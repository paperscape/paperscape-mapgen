#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysql/mysql.h>

#include "xiwilib.h"
#include "common.h"
#include "mysql.h"

#define VSTR_0 (0)
#define VSTR_1 (1)
#define VSTR_2 (2)
#define VSTR_MAX (3)

typedef struct _env_t {
    vstr_t *vstr[VSTR_MAX];
    bool close_mysql;
    MYSQL mysql;
    const char *maincat;
    int num_papers;
    paper_t *papers;
} env_t;

static bool have_error(env_t *env) {
    printf("MySQL error %d: %s\n", mysql_errno(&env->mysql), mysql_error(&env->mysql));
    return false;
}

static bool env_set_up(env_t* env) {
    for (int i = 0; i < VSTR_MAX; i++) {
        env->vstr[i] = vstr_new();
    }
    env->close_mysql = false;
    env->num_papers = 0;
    env->papers = NULL;

    // initialise the connection object
    if (mysql_init(&env->mysql) == NULL) {
        have_error(env);
        return false;
    }
    env->close_mysql = true;

    // connect to the MySQL server
    if (mysql_real_connect(&env->mysql, "localhost", "hidden", "hidden", "xiwi", 0, NULL, 0) == NULL) {
        if (mysql_real_connect(&env->mysql, "localhost", "hidden", "hidden", "xiwi", 0, "/home/damien/mysql/mysql.sock", 0) == NULL) {
            have_error(env);
            return false;
        }
    }

    return true;
}

static void env_finish(env_t* env) {
    for (int i = 0; i < VSTR_MAX; i++) {
        vstr_free(env->vstr[i]);
    }
    if (env->close_mysql) {
        mysql_close(&env->mysql);
    }
}

static bool env_query_one_row(env_t *env, const char *q, int expected_num_fields, MYSQL_RES **result) {
    if (mysql_query(&env->mysql, q) != 0) {
        return have_error(env);
    }
    if ((*result = mysql_store_result(&env->mysql)) == NULL) {
        return have_error(env);
    }
    if (mysql_num_rows(*result) != 1) {
        printf("env_query_one_row: expecting only 1 result, got %llu\n", mysql_num_rows(*result));
        mysql_free_result(*result);
        return false;
    }
    if (mysql_num_fields(*result) != expected_num_fields) {
        printf("env_query_one_row: expecting %d fields, got %u\n", expected_num_fields, mysql_num_fields(*result));
        mysql_free_result(*result);
        return false;
    }
    return true;
}

static bool env_query_many_rows(env_t *env, const char *q, int expected_num_fields, MYSQL_RES **result) {
    if (mysql_query(&env->mysql, q) != 0) {
        return have_error(env);
    }
    if ((*result = mysql_use_result(&env->mysql)) == NULL) {
        return have_error(env);
    }
    if (mysql_num_fields(*result) != expected_num_fields) {
        printf("env_query_many_rows: expecting %d fields, got %u\n", expected_num_fields, mysql_num_fields(*result));
        mysql_free_result(*result);
        return false;
    }
    return true;
}

/*
static bool env_query_no_result(env_t *env, const char *q, unsigned long len) {
    if (mysql_real_query(&env->mysql, q, len) != 0) {
        return have_error(env);
    }
    return true;
}
*/

static bool env_get_num_ids(env_t *env, int *num_ids) {
    MYSQL_RES *result;
    if (!env_query_one_row(env, "SELECT count(id) FROM meta_data", 1, &result)) {
        return false;
    }
    MYSQL_ROW row = mysql_fetch_row(result);
    *num_ids = atoi(row[0]);
    mysql_free_result(result);
    return true;
}

static int paper_cmp_id(const void *in1, const void *in2) {
    paper_t *p1 = (paper_t *)in1;
    paper_t *p2 = (paper_t *)in2;
    return p1->id - p2->id;
}

static bool env_load_ids(env_t *env, const char *maincat) {
    MYSQL_RES *result;
    MYSQL_ROW row;

    printf("reading ids from meta_data\n");

    // get the number of ids, so we can allocate the correct amount of memory
    int num_ids;
    if (!env_get_num_ids(env, &num_ids)) {
        return false;
    }

    // allocate memory for the papers
    env->papers = m_new(paper_t, num_ids);
    if (env->papers == NULL) {
        return false;
    }

    // get the ids
    vstr_t *vstr = env->vstr[VSTR_0];
    vstr_reset(vstr);
    vstr_printf(vstr, "SELECT id,maincat,authors,title FROM meta_data");
    if (maincat != NULL && maincat[0] != 0) {
        env->maincat = maincat;
        vstr_printf(vstr, " WHERE (maincat='%s' or maincat='hep-ph' or maincat='gr-qc')", maincat);
        vstr_printf(vstr, " AND id>=1992500000 AND id<2000000000");
    } else {
        env->maincat = NULL;
    }
    if (vstr_had_error(vstr)) {
        return false;
    }

    if (!env_query_many_rows(env, vstr_str(vstr), 4, &result)) {
        return false;
    }
    int i = 0;
    while ((row = mysql_fetch_row(result))) {
        if (i >= num_ids) {
            printf("got more ids than expected\n");
            mysql_free_result(result);
            return false;
        }
        int id = atoi(row[0]);
        paper_t *paper = &env->papers[i];
        paper->id = id;
        paper->num_refs = 0;
        paper->num_cites = 0;
        paper->refs = NULL;
        if (strcmp(row[1], "hep-th") == 0) {
            paper->maincat = 1;
        } else if (strcmp(row[1], "hep-ph") == 0) {
            paper->maincat = 2;
        } else {
            paper->maincat = 3;
        }
        paper->authors = strdup(row[2]);
        paper->title = strdup(row[3]);
        i += 1;
    }
    env->num_papers = i;
    mysql_free_result(result);

    // sort the papers array by id
    qsort(env->papers, env->num_papers, sizeof(paper_t), paper_cmp_id);

    // assign the index based on their sorted position
    for (int i = 0; i < env->num_papers; i++) {
        env->papers[i].index = i;
    }

    printf("read %d ids\n", env->num_papers);

    return true;
}

static paper_t *env_get_paper_by_id(env_t *env, int id) {
    int lo = 0;
    int hi = env->num_papers - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (id == env->papers[mid].id) {
            return &env->papers[mid];
        } else if (id < env->papers[mid].id) {
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }
    return NULL;
}

static bool env_load_refs(env_t *env, unsigned int min_id) {
    MYSQL_RES *result;
    MYSQL_ROW row;
    unsigned long *lens;

    if (min_id == 0) {
        printf("reading pcite\n");
    } else {
        printf("reading pcite for id>=%u\n", min_id);
    }

    // get the refs blobs from the pcite table
    vstr_t *vstr = env->vstr[VSTR_0];
    vstr_reset(vstr);
    vstr_printf(vstr, "SELECT id,refs FROM pcite");
    if (min_id > 0) {
        vstr_printf(vstr, " WHERE id>=%u", min_id);
    }
    if (vstr_had_error(vstr)) {
        return false;
    }
    if (!env_query_many_rows(env, vstr_str(vstr), 2, &result)) {
        return false;
    }

    int total_refs = 0;
    while ((row = mysql_fetch_row(result))) {
        lens = mysql_fetch_lengths(result);
        paper_t *paper = env_get_paper_by_id(env, atoi(row[0]));
        if (paper != NULL) {
            unsigned long len = lens[1];
            if (len == 0) {
                paper->num_refs = 0;
                paper->refs = NULL;
            } else {
                if (len % 10 != 0) {
                    printf("length of refs blob should be a multiple of 10; got %lu\n", len);
                    mysql_free_result(result);
                    return false;
                }
                paper->refs = m_new(paper_t*, len / 10);
                if (paper->refs == NULL) {
                    mysql_free_result(result);
                    return false;
                }
                paper->num_refs = 0;
                for (int i = 0; i < len; i += 10) {
                    byte *buf = (byte*)row[1] + i;
                    unsigned int id = decode_le32(buf + 0);
                    paper->refs[paper->num_refs] = env_get_paper_by_id(env, id);
                    if (paper->refs[paper->num_refs] != NULL) {
                        paper->refs[paper->num_refs]->num_cites += 1;
                        paper->num_refs++;
                    }
                }
                total_refs += paper->num_refs;
            }
        }
    }
    mysql_free_result(result);

    printf("read %d total refs\n", total_refs);

    return true;
}

static bool env_build_cites(env_t *env) {
    printf("building citation links\n");

    // allocate memory for cites for each paper
    for (int i = 0; i < env->num_papers; i++) {
        paper_t *paper = &env->papers[i];
        if (paper->num_cites > 0) {
            paper->cites = m_new(paper_t*, paper->num_cites);
            if (paper->cites == NULL) {
                return false;
            }
        }
        // use num cites to count which entry in the array we are up to when inserting cite links
        paper->num_cites = 0;
    }

    // link the cites
    for (int i = 0; i < env->num_papers; i++) {
        paper_t *paper = &env->papers[i];
        for (int j = 0; j < paper->num_refs; j++) {
            paper_t *ref_paper = paper->refs[j];
            ref_paper->cites[ref_paper->num_cites++] = paper;
        }
    }

    return true;
}

bool load_papers_from_mysql(const char *wanted_maincat, int *num_papers_out, paper_t **papers_out) {
    // set up environment
    env_t env;
    if (!env_set_up(&env)) {
        env_finish(&env);
        return false;
    }

    // load the DB
    env_load_ids(&env, wanted_maincat);
    env_load_refs(&env, 0);
    env_build_cites(&env);

    // pull down the MySQL environment (doesn't free the papers)
    env_finish(&env);

    // return the papers
    *num_papers_out = env.num_papers;
    *papers_out = env.papers;

    return true;
}
