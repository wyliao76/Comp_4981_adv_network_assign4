#include "database.h"
#include "args.h"
#include "utils.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#pragma GCC diagnostic ignored "-Waggregate-return"

static ssize_t secure_cmp(const void *a, const void *b, size_t size);

static ssize_t secure_cmp(const void *a, const void *b, size_t size)
{
    const uint8_t *x    = (const uint8_t *)a;
    const uint8_t *y    = (const uint8_t *)b;
    uint8_t        diff = 0;

    for(size_t i = 0; i < size; i++)
    {
        diff |= x[i] ^ y[i];    // XOR accumulates differences
    }

    return diff;    // 0 means equal, nonzero means different
}

ssize_t database_open(DBO *dbo, int *err)
{
    dbo->db = dbm_open(dbo->name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if(!dbo->db)
    {
        perror("dbm_open failed");
        *err = errno;
        return -1;
    }
    return 0;
}

int store_string(DBM *db, const char *key, const char *value)
{
    const_datum key_datum   = MAKE_CONST_DATUM(key);
    const_datum value_datum = MAKE_CONST_DATUM(value);

    return dbm_store(db, *(datum *)&key_datum, *(datum *)&value_datum, DBM_REPLACE);
}

int store_int(DBM *db, const char *key, int value)
{
    const_datum key_datum = MAKE_CONST_DATUM(key);
    datum       value_datum;
    int         result;

    value_datum.dptr = (char *)malloc(TO_SIZE_T(sizeof(int)));

    if(value_datum.dptr == NULL)
    {
        return -1;
    }

    memcpy(value_datum.dptr, &value, sizeof(int));
    value_datum.dsize = sizeof(int);

    result = dbm_store(db, *(datum *)&key_datum, value_datum, DBM_REPLACE);

    free(value_datum.dptr);
    return result;
}

char *retrieve_string(DBM *db, const char *key)
{
    const_datum key_datum;
    datum       result;
    char       *retrieved_str;

    key_datum = MAKE_CONST_DATUM(key);

    result = dbm_fetch(db, *(datum *)&key_datum);

    if(result.dptr == NULL)
    {
        return NULL;
    }

    retrieved_str = (char *)malloc(TO_SIZE_T(result.dsize));

    if(!retrieved_str)
    {
        return NULL;
    }

    memcpy(retrieved_str, result.dptr, TO_SIZE_T(result.dsize));

    return retrieved_str;
}

int retrieve_int(DBM *db, const char *key, int *result)
{
    datum       fetched;
    const_datum key_datum = MAKE_CONST_DATUM(key);

    fetched = dbm_fetch(db, *(datum *)&key_datum);

    if(fetched.dptr == NULL || fetched.dsize != sizeof(int))
    {
        return -1;
    }

    memcpy(result, fetched.dptr, sizeof(int));

    return 0;
}

ssize_t verify_user(DBM *db, const void *key, size_t k_size, const void *value, size_t v_size)
{
    const_datum key_datum;
    datum       result;
    ssize_t     match;

    key_datum = MAKE_CONST_DATUM_BYTE(key, k_size);

    result = dbm_fetch(db, *(datum *)&key_datum);

    if(result.dptr == NULL)
    {
        return -1;
    }

    printf("result.dsize: %zu\n", TO_SIZE_T(result.dsize));

    if(TO_SIZE_T(result.dsize) != v_size)
    {
        return -2;
    }

    match = secure_cmp(result.dptr, value, TO_SIZE_T(result.dsize));

    printf("match: %d\n", (int)match);
    if(match != 0)
    {
        return -3;
    }
    return 0;
}
