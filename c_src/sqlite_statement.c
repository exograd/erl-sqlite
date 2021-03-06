// Copyright (c) 2021 Exograd SAS.
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
// SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR
// IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include "sqlite_nif.h"

static int
esqlite_inspect_prepare_flags(ErlNifEnv *, ERL_NIF_TERM, unsigned int *);

void
esqlite_statement_delete(ErlNifEnv *env, void *ptr) {
        sqlite3_finalize((struct sqlite3_stmt *)ptr);
}

int
esqlite_inspect_statement(ErlNifEnv *env, ERL_NIF_TERM term,
                         struct sqlite3_stmt **opstmt) {
        struct esqlite_nif_data *nif_data;
        ErlNifResourceType *resource_type;
        struct sqlite3_stmt **pstmt;
        int ret;

        nif_data = enif_priv_data(env);
        resource_type = nif_data->statement_resource_type;

        ret = enif_get_resource(env, term, resource_type, (void **)&pstmt);
        if (ret == 0)
                return 0;

        *opstmt = *pstmt;

        return 1;
}

ERL_NIF_TERM
esqlite_prepare(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
        struct esqlite_nif_data *nif_data;
        struct sqlite3 *db;
        struct sqlite3_stmt *stmt, **pstmt;
        ERL_NIF_TERM stmt_resource, tail_term, result_term;
        const char *tail;
        char *query;
        unsigned int flags;
        int ret;

        nif_data = enif_priv_data(env);

        if (argc != 3)
                return enif_make_badarg(env);

        if (esqlite_inspect_database(env, argv[0], &db) == 0)
                return enif_make_badarg(env);

        if (esqlite_inspect_binary_string(env, argv[1], &query) == 0)
                return enif_make_badarg(env);

        if (esqlite_inspect_prepare_flags(env, argv[2], &flags) == 0)
                return enif_make_badarg(env);

        ret = sqlite3_prepare_v3(db, query, -1, flags, &stmt, &tail);
        if (ret != SQLITE_OK) {
                ERL_NIF_TERM reason;

                reason = esqlite_result_code(env, ret);

                enif_free(query);

                return esqlite_error_tuple(env, reason);
        }

        tail_term = esqlite_binary_string(env, tail);

        enif_free(query);

        pstmt = enif_alloc_resource(nif_data->statement_resource_type,
                                    sizeof(*pstmt));
        *pstmt = stmt;
        stmt_resource = enif_make_resource(env, pstmt);

        result_term = enif_make_tuple2(env, stmt_resource, tail_term);

        return esqlite_ok_tuple(env, result_term);
}

ERL_NIF_TERM
esqlite_finalize(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
        struct sqlite3_stmt *stmt;

        if (argc != 1)
                return enif_make_badarg(env);

        if (esqlite_inspect_statement(env, argv[0], &stmt) == 0)
                return enif_make_badarg(env);

        sqlite3_finalize(stmt);

        return enif_make_atom(env, "ok");
}

ERL_NIF_TERM
esqlite_step(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
        struct sqlite3_stmt *stmt;
        ERL_NIF_TERM reason;
        int ret;

        if (argc != 1)
                return enif_make_badarg(env);

        if (esqlite_inspect_statement(env, argv[0], &stmt) == 0)
                return enif_make_badarg(env);

        ret = sqlite3_step(stmt);

        reason = esqlite_result_code(env, ret);

        if (ret != SQLITE_ROW && ret != SQLITE_DONE)
                return esqlite_error_tuple(env, reason);

        return esqlite_ok_tuple(env, reason);
}

ERL_NIF_TERM
esqlite_reset(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
        struct sqlite3_stmt *stmt;
        int ret;

        if (argc != 1)
                return enif_make_badarg(env);

        if (esqlite_inspect_statement(env, argv[0], &stmt) == 0)
                return enif_make_badarg(env);

        ret = sqlite3_reset(stmt);
        if (ret != SQLITE_OK) {
                ERL_NIF_TERM reason;

                reason = esqlite_result_code(env, ret);

                return esqlite_error_tuple(env, reason);
        }

        return enif_make_atom(env, "ok");
}

ERL_NIF_TERM
esqlite_column_count(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
        struct sqlite3_stmt *stmt;
        int count;

        if (argc != 1)
                return enif_make_badarg(env);

        if (esqlite_inspect_statement(env, argv[0], &stmt) == 0)
                return enif_make_badarg(env);

        count = sqlite3_column_count(stmt);

        return enif_make_int(env, count);
}

ERL_NIF_TERM
esqlite_column_type(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
        struct sqlite3_stmt *stmt;
        ERL_NIF_TERM error;
        int column, type;

        if (argc != 2)
                return enif_make_badarg(env);

        if (esqlite_inspect_statement(env, argv[0], &stmt) == 0)
                return enif_make_badarg(env);

        if (enif_get_int(env, argv[1], &column) == 0)
                return enif_make_badarg(env);

        type = sqlite3_column_type(stmt, column);

        switch (type) {
        case SQLITE_BLOB:
                return enif_make_atom(env, "blob");
        case SQLITE_FLOAT:
                return enif_make_atom(env, "float");
        case SQLITE_INTEGER:
                return enif_make_atom(env, "integer");
        case SQLITE_NULL:
                return enif_make_atom(env, "null");
        case SQLITE_TEXT:
                return enif_make_atom(env, "text");
        }

        error = enif_make_tuple2(env, enif_make_atom(env, "unknown_datatype"),
                                 enif_make_int(env, type));
        return enif_raise_exception(env, error);
}

ERL_NIF_TERM
esqlite_column_bytes(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
        struct sqlite3_stmt *stmt;
        int column, bytes;

        if (argc != 2)
                return enif_make_badarg(env);

        if (esqlite_inspect_statement(env, argv[0], &stmt) == 0)
                return enif_make_badarg(env);

        if (enif_get_int(env, argv[1], &column) == 0)
                return enif_make_badarg(env);

        bytes = sqlite3_column_bytes(stmt, column);

        return enif_make_int(env, bytes);
}

ERL_NIF_TERM
esqlite_column_blob(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
        struct sqlite3_stmt *stmt;
        unsigned char *buf;
        const void *blob;
        int column, bytes;
        ERL_NIF_TERM term;

        if (argc != 2)
                return enif_make_badarg(env);

        if (esqlite_inspect_statement(env, argv[0], &stmt) == 0)
                return enif_make_badarg(env);

        if (enif_get_int(env, argv[1], &column) == 0)
                return enif_make_badarg(env);

        blob = sqlite3_column_blob(stmt, column);
        bytes = sqlite3_column_bytes(stmt, column);

        buf = enif_make_new_binary(env, (size_t)bytes, &term);
        memcpy(buf, blob, (size_t)bytes);

        return term;
}

ERL_NIF_TERM
esqlite_column_double(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
        struct sqlite3_stmt *stmt;
        int column;
        double d;

        if (argc != 2)
                return enif_make_badarg(env);

        if (esqlite_inspect_statement(env, argv[0], &stmt) == 0)
                return enif_make_badarg(env);

        if (enif_get_int(env, argv[1], &column) == 0)
                return enif_make_badarg(env);

        d = sqlite3_column_double(stmt, column);

        return enif_make_double(env, d);
}

ERL_NIF_TERM
esqlite_column_int64(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
        struct sqlite3_stmt *stmt;
        int column;
        int64_t i;

        if (argc != 2)
                return enif_make_badarg(env);

        if (esqlite_inspect_statement(env, argv[0], &stmt) == 0)
                return enif_make_badarg(env);

        if (enif_get_int(env, argv[1], &column) == 0)
                return enif_make_badarg(env);

        i = sqlite3_column_int64(stmt, column);

        return enif_make_int64(env, i);
}

ERL_NIF_TERM
esqlite_column_text(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
        struct sqlite3_stmt *stmt;
        unsigned char *buf;
        const void *text;
        int column, bytes;
        ERL_NIF_TERM term;

        if (argc != 2)
                return enif_make_badarg(env);

        if (esqlite_inspect_statement(env, argv[0], &stmt) == 0)
                return enif_make_badarg(env);

        if (enif_get_int(env, argv[1], &column) == 0)
                return enif_make_badarg(env);

        text = sqlite3_column_text(stmt, column);
        bytes = sqlite3_column_bytes(stmt, column);

        buf = enif_make_new_binary(env, (size_t)bytes, &term);
        memcpy(buf, text, (size_t)bytes);

        return term;
}

ERL_NIF_TERM
esqlite_bind_blob64(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
        struct sqlite3_stmt *stmt;
        ErlNifBinary binary;
        int parameter, ret;

        if (argc != 3)
                return enif_make_badarg(env);

        if (esqlite_inspect_statement(env, argv[0], &stmt) == 0)
                return enif_make_badarg(env);

        if (enif_get_int(env, argv[1], &parameter) == 0)
                return enif_make_badarg(env);

        if (enif_inspect_binary(env, argv[2], &binary) == 0)
                return 0;

        ret = sqlite3_bind_blob64(stmt, parameter, binary.data, binary.size,
                                  SQLITE_TRANSIENT);
        if (ret != SQLITE_OK) {
                ERL_NIF_TERM reason;

                reason = esqlite_result_code(env, ret);

                return esqlite_error_tuple(env, reason);
        }

        return enif_make_atom(env, "ok");
}

ERL_NIF_TERM
esqlite_bind_double(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
        struct sqlite3_stmt *stmt;
        int parameter, ret;
        double value;

        if (argc != 3)
                return enif_make_badarg(env);

        if (esqlite_inspect_statement(env, argv[0], &stmt) == 0)
                return enif_make_badarg(env);

        if (enif_get_int(env, argv[1], &parameter) == 0)
                return enif_make_badarg(env);

        if (enif_get_double(env, argv[2], &value) == 0)
                return enif_make_badarg(env);

        ret = sqlite3_bind_double(stmt, parameter, value);
        if (ret != SQLITE_OK) {
                ERL_NIF_TERM reason;

                reason = esqlite_result_code(env, ret);

                return esqlite_error_tuple(env, reason);
        }

        return enif_make_atom(env, "ok");
}

ERL_NIF_TERM
esqlite_bind_int64(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
        struct sqlite3_stmt *stmt;
        int parameter, ret;
        int64_t value;

        if (argc != 3)
                return enif_make_badarg(env);

        if (esqlite_inspect_statement(env, argv[0], &stmt) == 0)
                return enif_make_badarg(env);

        if (enif_get_int(env, argv[1], &parameter) == 0)
                return enif_make_badarg(env);

        if (enif_get_int64(env, argv[2], &value) == 0)
                return enif_make_badarg(env);

        ret = sqlite3_bind_int64(stmt, parameter, value);
        if (ret != SQLITE_OK) {
                ERL_NIF_TERM reason;

                reason = esqlite_result_code(env, ret);

                return esqlite_error_tuple(env, reason);
        }

        return enif_make_atom(env, "ok");
}

ERL_NIF_TERM
esqlite_bind_null(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
        struct sqlite3_stmt *stmt;
        int parameter, ret;

        if (argc != 2)
                return enif_make_badarg(env);

        if (esqlite_inspect_statement(env, argv[0], &stmt) == 0)
                return enif_make_badarg(env);

        if (enif_get_int(env, argv[1], &parameter) == 0)
                return enif_make_badarg(env);

        ret = sqlite3_bind_null(stmt, parameter);
        if (ret != SQLITE_OK) {
                ERL_NIF_TERM reason;

                reason = esqlite_result_code(env, ret);

                return esqlite_error_tuple(env, reason);
        }

        return enif_make_atom(env, "ok");
}

ERL_NIF_TERM
esqlite_bind_text64(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]) {
        struct sqlite3_stmt *stmt;
        ErlNifBinary binary;
        int parameter, ret;

        if (argc != 3)
                return enif_make_badarg(env);

        if (esqlite_inspect_statement(env, argv[0], &stmt) == 0)
                return enif_make_badarg(env);

        if (enif_get_int(env, argv[1], &parameter) == 0)
                return enif_make_badarg(env);

        if (enif_inspect_binary(env, argv[2], &binary) == 0)
                return 0;

        ret = sqlite3_bind_text64(stmt, parameter, (const char*)binary.data,
                                  binary.size, SQLITE_TRANSIENT, SQLITE_UTF8);
        if (ret != SQLITE_OK) {
                ERL_NIF_TERM reason;

                reason = esqlite_result_code(env, ret);

                return esqlite_error_tuple(env, reason);
        }

        return enif_make_atom(env, "ok");
}

static int
esqlite_inspect_prepare_flags(ErlNifEnv *env, ERL_NIF_TERM list,
                              unsigned int *pflags) {
        ERL_NIF_TERM head, tail;
        unsigned int flags;

        if (!enif_is_list(env, list))
                return 0;

        flags = 0;

        while (enif_get_list_cell(env, list, &head, &tail) == 1) {
                if (esqlite_is_atom(env, head, "persistent")) {
                        flags |= SQLITE_PREPARE_PERSISTENT;
                } else if (esqlite_is_atom(env, head, "normalize")) {
                        flags |= SQLITE_PREPARE_NORMALIZE;
                } else if (esqlite_is_atom(env, head, "no_vtab")) {
                        flags |= SQLITE_PREPARE_NO_VTAB;
                } else {
                        return 0;
                }

                list = tail;
        }

        *pflags = flags;

        return 1;
}
