/*
  +----------------------------------------------------------------------+
  | zredis                                                              |
  +----------------------------------------------------------------------+
  | Licensed under the Apache License, Version 2.0 (the "License"); you  |
  | may not use this file except in compliance with the License. You may |
  | obtain a copy of the License at                                      |
  | http://www.apache.org/licenses/LICENSE-2.0                           |
  |                                                                      |
  | Unless required by applicable law or agreed to in writing, software  |
  | distributed under the License is distributed on an "AS IS" BASIS,    |
  | WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or      |
  | implied. See the License for the specific language governing         |
  | permissions and limitations under the License.                       |
  +----------------------------------------------------------------------+
  | Author: Adam Saponara <adam@atoi.cc>                                 |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "ext/standard/basic_functions.h"
#include "php_zredis.h"

#include "zend_exceptions.h"
#include "zend_interfaces.h"

#include <hiredis.h>

static zend_object_handlers zredis_obj_handlers;
static zend_class_entry *zredis_ce;
static zend_class_entry *zredis_exception_ce;
static zredis_t *zredis_client;
static int persisent = 0;

ZEND_BEGIN_ARG_INFO_EX(arginfo_zredis_none, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_zredis_call, 0, 0, 2)
    ZEND_ARG_INFO(0, func_name)
    ZEND_ARG_INFO(0, func_args)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_zredis_connect, 0, 0, 0)
    ZEND_ARG_INFO(0, ip)
    ZEND_ARG_INFO(0, port)
    ZEND_ARG_INFO(0, timeout)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_zredis_pconnect, 0, 0, 0)
    ZEND_ARG_INFO(0, ip)
    ZEND_ARG_INFO(0, port)
    ZEND_ARG_INFO(0, timeout)
    ZEND_ARG_INFO(0, persistent)   
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_zredis_set_timeout, 0, 0, 1)
    ZEND_ARG_INFO(0, timeout_us)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_zredis_set_keep_alive_int, 0, 0, 1)
    ZEND_ARG_INFO(0, interval_s)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_zredis_set_max_read_buf, 0, 0, 1)
    ZEND_ARG_INFO(0, max_bytes)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_zredis_send_raw, 0, 0, 1)
    ZEND_ARG_INFO(0, command_args)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_zredis_send_raw_array, 0, 0, 1)
    ZEND_ARG_INFO(0, command_argv)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_zredis_set_throw_exceptions, 0, 0, 1)
    ZEND_ARG_INFO(0, true_or_false)
ZEND_END_ARG_INFO()

#if PHP_MAJOR_VERSION >= 7
    typedef size_t strlen_t;
    #define Z_ZREDIS_P(zv) zredis_obj_fetch(Z_OBJ_P((zv)))
    #define MAKE_STD_ZVAL(zv) do { \
        zval _sz; \
        (zv) = &_sz; \
    } while (0)
#else
    typedef int strlen_t;
    typedef char zend_string;
    #define Z_ZREDIS_P(zv) zredis_obj_fetch(zv TSRMLS_CC)
    #define ZEND_HASH_FOREACH_VAL(_ht, _ppv) do { \
        HashPosition _pos; \
        for (zend_hash_internal_pointer_reset_ex((_ht), &_pos); \
            zend_hash_get_current_data_ex((_ht), (void **) &(_ppv), &_pos) == SUCCESS; \
            zend_hash_move_forward_ex((_ht), &_pos) ) {
    #define ZEND_HASH_FOREACH_END() } } while (0)
#endif

/* Macro to set custom err and errstr together */
#define PHP_ZREDIS_SET_ERROR_EX(client, perr, perrstr) do { \
    (client)->err = (perr); \
    snprintf((client)->errstr, sizeof((client)->errstr), "%s", (perrstr)); \
    if ((client)->throw_exceptions) { \
        zend_throw_exception(zredis_exception_ce, (client)->errstr, (client)->err); \
    } \
} while(0)

/* Macro to set err and errstr together */
#define PHP_ZREDIS_SET_ERROR(client) do { \
    PHP_ZREDIS_SET_ERROR_EX((client), (client)->ctx->err, (client)->ctx->errstr); \
} while(0)

/* Macro to ensure ctx is not NULL */
#define PHP_ZREDIS_ENSURE_CTX(client) do { \
    if (!(client)->ctx) { \
        PHP_ZREDIS_SET_ERROR_EX(client, REDIS_ERR, "No redisContext"); \
        RETURN_FALSE; \
    } \
} while(0)

/* Macro to handle returning/throwing a zval to userland */
#if PHP_MAJOR_VERSION >= 7
    #define PHP_ZREDIS_RETVAL_COPY_DTOR 1
#else
    #define PHP_ZREDIS_RETVAL_COPY_DTOR 0
#endif
#define PHP_ZREDIS_RETURN_OR_THROW(client, zv) do { \
    if (Z_TYPE_P(zv) == IS_OBJECT && instanceof_function(Z_OBJCE_P(zv), zredis_exception_ce)) { \
        PHP_ZREDIS_SET_ERROR_EX((client), REDIS_ERR, _hidreis_get_exception_message(zv)); \
        if (PHP_ZREDIS_RETVAL_COPY_DTOR) zval_dtor(zv); \
        RETVAL_FALSE; \
    } else { \
        RETVAL_ZVAL((zv), PHP_ZREDIS_RETVAL_COPY_DTOR, PHP_ZREDIS_RETVAL_COPY_DTOR); \
    } \
} while(0)

/* Return exception message */
static char* _hidreis_get_exception_message(zval* ex) {
    zval* zp = NULL;
    #if PHP_MAJOR_VERSION >= 7
        zval z = {0};
        zp = zend_read_property(zredis_exception_ce, ex, "message", sizeof("message")-1, 1, &z);
    #else
        zp = zend_read_property(zredis_exception_ce, ex, "message", sizeof("message")-1, 1);
    #endif
    return Z_STRVAL_P(zp);
}

/* Fetch zredis_t inside zval */
#if PHP_MAJOR_VERSION >= 7
static inline zredis_t* zredis_obj_fetch(zend_object* obj) {
    return (zredis_t*)((char*)(obj) - XtOffsetOf(zredis_t, std));
}
#else
static inline zredis_t* zredis_obj_fetch(zval* obj TSRMLS_DC) {
    return (zredis_t*)zend_object_store_get_object(obj TSRMLS_CC);
}
#endif

/* Allocate/deallocate zredis_t object */
#if PHP_MAJOR_VERSION >= 7
static void zredis_obj_free(zend_object *object) {
    zredis_t* client;
    client = zredis_obj_fetch(object);
    if (!client) {
        return;
    }
    if (client->ctx) {
        redisFree(client->ctx);
        client->ctx =NULL;
    }
    zend_object_std_dtor(&client->std);
    efree(client);
}
static inline zend_object* zredis_obj_new(zend_class_entry *ce) {
    zredis_t* client;
    client = ecalloc(1, sizeof(zredis_t) + zend_object_properties_size(ce));
    zend_object_std_init(&client->std, ce);
    object_properties_init(&client->std, ce);
    client->std.handlers = &zredis_obj_handlers;
    return &client->std;
}
#else
static void zredis_obj_free(void *obj TSRMLS_DC) {
    zredis_t *client = (zredis_t*)obj;
    if (!client) {
        return;
    }
    if (client->ctx) {
        redisFree(client->ctx);
        client->ctx =NULL;
    }
    zend_object_std_dtor(&client->std TSRMLS_CC);
    efree(client);
}
static inline zend_object_value zredis_obj_new(zend_class_entry *ce TSRMLS_DC) {
    zredis_t* client;
    zend_object_value retval;
    client = ecalloc(1, sizeof(zredis_t));
    client->throw_exceptions = 1;
    zend_object_std_init(&client->std, ce TSRMLS_CC);
    object_properties_init((zend_object*)client, ce);
    retval.handle = zend_objects_store_put(client, NULL, zredis_obj_free, NULL TSRMLS_CC);
    retval.handlers = (zend_object_handlers*)&zredis_obj_handlers;
    return retval;
}
#endif

/* Throw errstr as exception */
static void _zredis_throw_err_exception(zredis_t* client) {
    zend_throw_exception(zredis_exception_ce, client->errstr, client->err);
}

/* Wrap redisSetTimeout */
static int _zredis_set_timeout(zredis_t* client, long timeout_us) {
    struct timeval timeout_tv;
    timeout_tv.tv_sec = timeout_us / 1000000;
    timeout_tv.tv_usec = timeout_us % 1000000;
    if (REDIS_OK != redisSetTimeout(client->ctx, timeout_tv)) {
        PHP_ZREDIS_SET_ERROR_EX(client, REDIS_ERR, "redisSetTimeout failed");
        return REDIS_ERR;
    }
    return REDIS_OK;
}

/* Wrap redisKeepAlive */
static int _zredis_set_keep_alive_int(zredis_t* client, int interval) {
    if (REDIS_OK != redisKeepAlive(client->ctx, interval)) {
        PHP_ZREDIS_SET_ERROR_EX(client, REDIS_ERR, "redisKeepAlive failed");
        return REDIS_ERR;
    }
    return REDIS_OK;
}

/* redisReplyObjectFunctions: Nest zval in parent array */
static zval* _zredis_replyobj_nest(const redisReadTask* task, zval* z) {
    zval* rv = z;
    if (task->parent) {
        zval* parent;
        parent = (zval*)task->parent->obj;
        assert(Z_TYPE_P(parent) == IS_ARRAY);
        #if PHP_MAJOR_VERSION >= 7
            rv = zend_hash_index_update(Z_ARRVAL_P(parent), task->idx, z);
        #else
            add_index_zval(parent, task->idx, z);
        #endif
    }
    return rv;
}

/* redisReplyObjectFunctions: Get zval to operate on */
static zval* _zredis_replyobj_get_zval(const redisReadTask* task, zval* stack_zval) {
    zval* rv;
    if (task->parent) {
        #if PHP_MAJOR_VERSION >= 7
            rv = stack_zval;
        #else
            MAKE_STD_ZVAL(rv);
        #endif
    } else {
        rv = (zval*)task->privdata;
    }
    return rv;
}

/* redisReplyObjectFunctions: Create string */
static void* zredis_replyobj_create_string(const redisReadTask* task, char* str, size_t len) {
    zval sz;
    zval* z = _zredis_replyobj_get_zval(task, &sz);
    if (task->type == REDIS_REPLY_ERROR) {
        object_init_ex(z, zredis_exception_ce);
        zend_update_property_stringl(zredis_exception_ce, z, "message", sizeof("message")-1, str, len);
    } else {
        #if PHP_MAJOR_VERSION >= 7
            ZVAL_STRINGL(z, str, len);
        #else
            ZVAL_STRINGL(z, str, len, 1);
        #endif
    }
    return (void*)_zredis_replyobj_nest(task, z);
}

/* redisReplyObjectFunctions: Create array */
static void* zredis_replyobj_create_array(const redisReadTask* task, int len) {
    zval sz;
    zval* z = _zredis_replyobj_get_zval(task, &sz);
    array_init_size(z, len);
    return (void*)_zredis_replyobj_nest(task, z);
}

/* redisReplyObjectFunctions: Create int */
static void* zredis_replyobj_create_integer(const redisReadTask* task, long long i) {
    zval sz;
    zval* z = _zredis_replyobj_get_zval(task, &sz);
    ZVAL_LONG(z, i);
    return (void*)_zredis_replyobj_nest(task, z);
}

/* redisReplyObjectFunctions: Create nil */
static void* zredis_replyobj_create_nil(const redisReadTask* task) {
    zval sz;
    zval* z = _zredis_replyobj_get_zval(task, &sz);
    ZVAL_NULL(z);
    return (void*)_zredis_replyobj_nest(task, z);
}

/* redisReplyObjectFunctions: Free object */
static void zredis_replyobj_free(void* obj) {
    // TODO Everything should get gc'd. Confirm this is the case when zredis
    //      invokes this function directly.
}

/* Declare reply object funcs */
static redisReplyObjectFunctions zredis_replyobj_funcs = {
    zredis_replyobj_create_string,
    zredis_replyobj_create_array,
    zredis_replyobj_create_integer,
    zredis_replyobj_create_nil,
    zredis_replyobj_free
};

/* Convert zval of type array to a C array of zvals. Call must free ret_zvals. */
static void _zredis_convert_zval_to_array_of_zvals(zval* arr, zval** ret_zvals, int* ret_num_zvals) {
    zval* zvals;
    zval* zv;
    int argc;
    int i;
    if (Z_TYPE_P(arr) != IS_ARRAY) {
        convert_to_array(arr);
    }
    argc = zend_hash_num_elements(Z_ARRVAL_P(arr));
    zvals = (zval*)safe_emalloc(argc, sizeof(zval), 0);
    i = 0;

    #if PHP_MAJOR_VERSION >= 7
        ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(arr), zv) {
            ZVAL_COPY_VALUE(&zvals[i], zv);
            i++;
        } ZEND_HASH_FOREACH_END();
    #else
        HashPosition hash_pos = NULL;
        zval** hash_entry = NULL;
        zend_hash_internal_pointer_reset_ex(Z_ARRVAL_P(arr), &hash_pos);
        while (zend_hash_get_current_data_ex(Z_ARRVAL_P(arr), (void**)&hash_entry, &hash_pos) == SUCCESS) {
            ZVAL_COPY_VALUE(&zvals[i], *hash_entry);
            i++;
            zend_hash_move_forward_ex(Z_ARRVAL_P(arr), &hash_pos);
        }
    #endif

    *ret_zvals = zvals;
    *ret_num_zvals = argc;
}

/* Actually send/queue a redis command. If `cmd` is not NULL, it is sent as the
   first token, followed by `args`. Is `is_append` is set,
   redisAppendCommandArgv is called instead of redisCommandArgv. */
static void _zredis_send_raw_array(INTERNAL_FUNCTION_PARAMETERS, zredis_t* client, char* cmd, zval* args, int argc, int is_append) {
    char** string_args;
    size_t* string_lens;
    zend_string** string_zstrs;
    int i, j;
    zval* zv;
    int num_strings;

    // Convert array of zvals to string + stringlen params. If cmd is not NULL
    // make it the first arg.
    num_strings = cmd ? argc + 1 : argc;
    string_args = (char**)safe_emalloc(num_strings, sizeof(char*), 0);
    string_lens = (size_t*)safe_emalloc(num_strings, sizeof(size_t), 0);
    string_zstrs = (zend_string**)safe_emalloc(num_strings, sizeof(zend_string*), 0);
    j = 0;
    if (cmd) {
        string_args[j] = cmd;
        string_lens[j] = strlen(cmd);
        string_zstrs[j] = NULL;
        j++;
    }
    for (i = 0; i < argc; i++, j++) {
        zval* _zp = &args[i];
        string_zstrs[j] = NULL;
        if (
            #if PHP_MAJOR_VERSION >= 7
                Z_TYPE_P(_zp) == IS_TRUE || Z_TYPE_P(_zp) == IS_FALSE
            #else
                Z_TYPE_P(_zp) == IS_BOOL
            #endif
        ) {
            string_args[j] = zend_is_true(_zp) ? "1" : "0";
            string_lens[j] = 1;
        } else {
            if (Z_TYPE_P(_zp) != IS_STRING) {
                #if PHP_MAJOR_VERSION >= 7
                    convert_to_string_ex(_zp);
                    string_zstrs[j] = Z_STR_P(_zp);
                #else
                    convert_to_string(_zp);
                    string_zstrs[j] = Z_STRVAL_P(_zp);
                #endif
            }
            string_args[j] = Z_STRVAL_P(_zp);
            string_lens[j] = Z_STRLEN_P(_zp);
        }
    }

    // Send/queue command
    if (is_append) {
        if (REDIS_OK != redisAppendCommandArgv(client->ctx, num_strings, (const char**)string_args, string_lens)) {
            PHP_ZREDIS_SET_ERROR(client);
            RETVAL_FALSE;
        } else {
            RETVAL_TRUE;
        }
    } else {
        redisReplyReaderSetPrivdata(client->ctx->reader, (void*)return_value);
        if (zv = (zval*)redisCommandArgv(client->ctx, num_strings, (const char**)string_args, string_lens)) {
            assert(zv == return_value);
            PHP_ZREDIS_RETURN_OR_THROW(client, return_value);
        } else {
            PHP_ZREDIS_SET_ERROR(client);
            RETVAL_FALSE;
        }
    }

    // Cleanup
    for (i = 0; i < num_strings; i++) {
        if (string_zstrs[i]) {
            #if PHP_MAJOR_VERSION >= 7
                zend_string_release(string_zstrs[i]);
            #else
                STR_FREE(string_zstrs[i]);
            #endif
        }
    }
    efree(string_zstrs);
    efree(string_args);
    efree(string_lens);
}

/* Prepare args for _zredis_send_raw_array */
static void _zredis_send_raw(INTERNAL_FUNCTION_PARAMETERS, int is_array, int is_append) {
    zredis_t* client;
    zval* zobj;
    int argc;
    zval* args;
    #if PHP_MAJOR_VERSION >= 7
        zval* varargs;
    #else
        zval*** varargs;
    #endif

    if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "O+", &zobj, zredis_ce, &varargs, &argc) == FAILURE) {
        RETURN_FALSE;
    }
    if ((is_array && argc != 1) || (!is_array && argc < 1)) {
        WRONG_PARAM_COUNT;
    }

    client = Z_ZREDIS_P(zobj);
    PHP_ZREDIS_ENSURE_CTX(client);

    #if PHP_MAJOR_VERSION >= 7
        if (is_array) {
            _zredis_convert_zval_to_array_of_zvals(varargs, &args, &argc);
        } else {
            args = varargs;
        }
    #else
        if (is_array) {
            _zredis_convert_zval_to_array_of_zvals(**varargs, &args, &argc);
        } else {
            int i;
            args = (zval*)safe_emalloc(argc, sizeof(zval), 0);
            for (i = 0; i < argc; i++) memcpy(&args[i], *(varargs[i]), sizeof(zval));
        }
    #endif

    _zredis_send_raw_array(INTERNAL_FUNCTION_PARAM_PASSTHRU, client, NULL, args, argc, is_append);

    #if PHP_MAJOR_VERSION >= 7
        if (is_array) efree(args);
    #else
        efree(args);
        efree(varargs);
    #endif
}

/* Invoked after connecting */
static int _zredis_conn_init(zredis_t* client) {
    int rc;
    rc = REDIS_OK;
    if (client->keep_alive_int_s >= 0) {
        if (REDIS_OK != _zredis_set_keep_alive_int(client, client->keep_alive_int_s)) {
            rc = REDIS_ERR;
        }
    }
    if (client->timeout_us >= 0) {
        if (REDIS_OK != _zredis_set_timeout(client, client->timeout_us)) {
            rc = REDIS_ERR;
        }
    }
    client->ctx->reader->maxbuf = client->max_read_buf;
    client->ctx->reader->fn = &zredis_replyobj_funcs;
    return rc;
}

/* Invoked before connecting and at __destruct */
static void _zredis_conn_deinit(zredis_t* client) {
    if(!persisent) {
        if (client->ctx) {
            redisFree(client->ctx);
        }
        client->ctx = NULL;
    }
}

/* {{{ proto void zredis::__construct()
   Constructor for zredis. */
PHP_METHOD(zredis, __construct) {
    zredis_t* client;
    if (zend_parse_parameters_none() == FAILURE) {
        return;
    }
    return_value = getThis();
    client = Z_ZREDIS_P(return_value);
    client->ctx = NULL;
    client->timeout_us = -1;
    client->keep_alive_int_s = -1;
    client->max_read_buf = REDIS_READER_MAX_BUF;
    client->throw_exceptions = 0;
}
/* }}} */

/* {{{ proto void zredis::__destruct()
   Destructor for zredis. */
PHP_METHOD(zredis, __destruct) {
    zredis_t* client;
    return_value = getThis();
    client = Z_ZREDIS_P(return_value);
    if(!persisent) {
        _zredis_conn_deinit(client);
    }
}
/* }}} */

/* {{{ proto void zredis::__call(string func, array args)
   Magic command handler. */
PHP_METHOD(zredis, __call) {
    zredis_t* client;
    char* ofunc;
    char* func;
    strlen_t func_len;
    zval* func_args;
    int func_argc;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "sa", &ofunc, &func_len, &func_args) == FAILURE) {
        RETURN_FALSE;
    }

    client = Z_ZREDIS_P(getThis());
    PHP_ZREDIS_ENSURE_CTX(client);

    func = estrndup(ofunc, func_len);
    php_strtoupper(func, func_len);

    _zredis_convert_zval_to_array_of_zvals(func_args, &func_args, &func_argc);
    _zredis_send_raw_array(INTERNAL_FUNCTION_PARAM_PASSTHRU, client, func, func_args, func_argc, 0);
    efree(func_args);
    efree(func);
}
/* }}} */

/* {{{ proto bool zredis_connect([string ip, int port , float timeout_s])
   Connect to a server via TCP. */
PHP_FUNCTION(zredis_connect) {
    zval* zobj;
    zredis_t* client;
    char* ip = NULL;
    strlen_t ip_len = 0;
    long port = 0;
    double timeout_s = 3;
    if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "O|sldl", &zobj, zredis_ce, &ip, &ip_len, &port, &timeout_s) == FAILURE) {
        RETURN_FALSE;
    }
    client = Z_ZREDIS_P(zobj);
    _zredis_conn_deinit(client);
    if (timeout_s >= 0) {
        client->timeout_us = (long)(timeout_s * 1000 * 1000);
    }
    client->ctx = NULL;
    if (ip && '/' == ip[0]) {
        client->ctx = redisConnectUnix(ip);
    } else {
        if(!ip) {
            ip = "127.0.0.1";
        }
        if(!port) {
            port = 6379;
        }
        client->ctx = redisConnect(ip, port);
    }
    if (!client->ctx) {
        PHP_ZREDIS_SET_ERROR_EX(client, REDIS_ERR, "redisConnect returned NULL");
        RETURN_FALSE;
    } else if (client->ctx->err) {
        PHP_ZREDIS_SET_ERROR(client);
        RETURN_FALSE;
    }
    if (REDIS_OK == _zredis_conn_init(client)) {
        RETURN_TRUE;
    }
    RETURN_FALSE;
}
/* }}} */

/* {{{ proto bool zredis_pconnect([string ip, int port, int timeout_s, string persistent_key])
   Connect to a server via . persistent connect*/
PHP_FUNCTION(zredis_pconnect) {
    zval* zobj;
    zredis_t* client;
    char* ip = NULL;
    strlen_t ip_len;
    long port = 0;
    double timeout_s = 3;
    if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "O|sld", &zobj, zredis_ce, &ip, &ip_len, &port, &timeout_s) == FAILURE) {
        RETURN_FALSE;
    }
    client = Z_ZREDIS_P(zobj);
    if(client->ctx == NULL) {
        if (timeout_s >= 0) {
            client->timeout_us = (long)(timeout_s * 1000 * 1000);
        }
        if (ip && '/' == ip[0]) {
            client->ctx = redisConnectUnix(ip);
        } else {
            if(!ip) {
                ip = "127.0.0.1";
            }
            if(!port) {
                port = 6379;
            }
            client->ctx = redisConnect(ip, port);
        }
        persisent = 1;
        if (!client->ctx) {
            PHP_ZREDIS_SET_ERROR_EX(client, REDIS_ERR, "redisConnect returned NULL");
            RETURN_FALSE;
        } else if (client->ctx->err) {
            PHP_ZREDIS_SET_ERROR(client);
            RETURN_FALSE;
        }
        if (REDIS_OK == _zredis_conn_init(client)) {
            zredis_client = client;
            RETURN_TRUE;
        }
        RETURN_FALSE;
    } 
    //check ping todo
    RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool zredis_reconnect()
   Reonnect to a server. */
#ifdef HAVE_ZREDIS_RECONNECT
PHP_FUNCTION(zredis_reconnect) {
    zval* zobj;
    zredis_t* client;
    char* path;
    size_t path_len;
    if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "O", &zobj, zredis_ce) == FAILURE) {
        RETURN_FALSE;
    }
    client = Z_ZREDIS_P(zobj);
    PHP_ZREDIS_ENSURE_CTX(client);
    if (REDIS_OK != redisReconnect(client->ctx)) {
        PHP_ZREDIS_SET_ERROR(client);
        RETURN_FALSE;
    }
    if (REDIS_OK != _zredis_conn_init(client)) {
        RETURN_FALSE;
    }
    RETURN_TRUE;
}
#endif
/* }}} */

/* {{{ proto bool zredis_set_timeout(int timeout_us)
   Set read/write timeout in microseconds. */
PHP_FUNCTION(zredis_set_timeout) {
    zval* zobj;
    zredis_t* client;
    long timeout_us;
    struct timeval timeout_tv;
    if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "Ol", &zobj, zredis_ce, &timeout_us) == FAILURE) {
        RETURN_FALSE;
    }
    client = Z_ZREDIS_P(zobj);
    client->timeout_us = timeout_us;
    if (!client->ctx) {
        RETURN_TRUE;
    }
    if (REDIS_OK != _zredis_set_timeout(client, timeout_us)) {
        RETURN_FALSE;
    }
    RETURN_TRUE;
}
/* }}} */

/* {{{ proto int zredis_get_timeout()
   Get read/write timeout in microseconds. */
PHP_FUNCTION(zredis_get_timeout) {
    zval* zobj;
    zredis_t* client;
    if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "O", &zobj, zredis_ce) == FAILURE) {
        RETURN_FALSE;
    }
    client = Z_ZREDIS_P(zobj);
    RETURN_LONG(client->timeout_us);
}
/* }}} */

/* {{{ proto bool zredis_set_keep_alive_int(int keep_alive_int_s)
   Set keep alive interval in seconds. */
PHP_FUNCTION(zredis_set_keep_alive_int) {
    zval* zobj;
    zredis_t* client;
    long keep_alive_int_s;
    if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "Ol", &zobj, zredis_ce, &keep_alive_int_s) == FAILURE) {
        RETURN_FALSE;
    }
    client = Z_ZREDIS_P(zobj);
    client->keep_alive_int_s = keep_alive_int_s;
    if (!client->ctx) {
        RETURN_TRUE;
    }
    if (REDIS_OK != _zredis_set_keep_alive_int(client, keep_alive_int_s)) {
        RETURN_FALSE;
    }
    RETURN_TRUE;
}
/* }}} */

/* {{{ proto int zredis_get_keep_alive_int()
   Get keep alive interval in seconds.*/
PHP_FUNCTION(zredis_get_keep_alive_int) {
    zval* zobj;
    zredis_t* client;
    if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "O", &zobj, zredis_ce) == FAILURE) {
        RETURN_FALSE;
    }
    client = Z_ZREDIS_P(zobj);
    RETURN_LONG(client->keep_alive_int_s);
}
/* }}} */

/* {{{ proto bool zredis_set_max_read_buf(int max_bytes)
   Set max read buffer in bytes. */
PHP_FUNCTION(zredis_set_max_read_buf) {
    zval* zobj;
    zredis_t* client;
    long max_bytes;
    if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "Ol", &zobj, zredis_ce, &max_bytes) == FAILURE) {
        RETURN_FALSE;
    }
    client = Z_ZREDIS_P(zobj);
    client->max_read_buf = max_bytes;
    if (client->ctx) {
        client->ctx->reader->maxbuf = max_bytes;
    }
    RETURN_TRUE;
}
/* }}} */

/* {{{ proto int zredis_get_max_read_buf()
   Get max read buffer in bytes. */
PHP_FUNCTION(zredis_get_max_read_buf) {
    zval* zobj;
    zredis_t* client;
    if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "O", &zobj, zredis_ce) == FAILURE) {
        RETURN_FALSE;
    }
    client = Z_ZREDIS_P(zobj);
    RETURN_LONG(client->max_read_buf);
}
/* }}} */

/* {{{ proto bool zredis_set_throw_exceptions(bool on_off)
   Set whether to throw exceptions on ERR replies from server. */
PHP_FUNCTION(zredis_set_throw_exceptions) {
    zval* zobj;
    zredis_t* client;
    zend_bool on_off;
    if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "Ob", &zobj, zredis_ce, &on_off) == FAILURE) {
        RETURN_FALSE;
    }
    client = Z_ZREDIS_P(zobj);
    client->throw_exceptions = on_off ? 1 : 0;
    RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool zredis_get_throw_exceptions()
   Get whether throw_exceptions is enabled. */
PHP_FUNCTION(zredis_get_throw_exceptions) {
    zval* zobj;
    zredis_t* client;
    if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "O", &zobj, zredis_ce) == FAILURE) {
        RETURN_FALSE;
    }
    client = Z_ZREDIS_P(zobj);
    RETURN_BOOL(client->throw_exceptions);
}
/* }}} */

/* {{{ proto mixed zredis_send_raw(string args...)
   Send command and return result. */
PHP_FUNCTION(zredis_send_raw) {
    _zredis_send_raw(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0, 0);
}
/* }}} */

/* {{{ proto string zredis_send_raw_array(array args)
   Send command and return result. */
PHP_FUNCTION(zredis_send_raw_array) {
    _zredis_send_raw(INTERNAL_FUNCTION_PARAM_PASSTHRU, 1, 0);
}
/* }}} */

/* {{{ proto string zredis_append_command(string args...)
   Append command to pipeline. */
PHP_FUNCTION(zredis_append_command) {
    _zredis_send_raw(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0, 1);
}
/* }}} */

/* {{{ proto string zredis_append_command_array(array args)
   Append command to pipeline. */
PHP_FUNCTION(zredis_append_command_array) {
    _zredis_send_raw(INTERNAL_FUNCTION_PARAM_PASSTHRU, 1, 1);
}
/* }}} */

/* {{{ proto string zredis_get_reply()
   Get reply from pipeline. */
PHP_FUNCTION(zredis_get_reply) {
    zval* zobj;
    zredis_t* client;
    zval* reply = NULL;
    if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "O", &zobj, zredis_ce) == FAILURE) {
        RETURN_FALSE;
    }
    client = Z_ZREDIS_P(zobj);
    PHP_ZREDIS_ENSURE_CTX(client);
    redisReplyReaderSetPrivdata(client->ctx->reader, (void*)return_value);
    if (REDIS_OK != redisGetReply(client->ctx, (void**)&reply)) {
        PHP_ZREDIS_SET_ERROR(client);
        RETURN_FALSE;
    } else if (reply) {
        assert(reply == return_value);
        PHP_ZREDIS_RETURN_OR_THROW(client, return_value);
    } else {
        PHP_ZREDIS_SET_ERROR_EX(client, REDIS_ERR, "redisGetReply returned NULL");
        RETURN_FALSE;
    }
}
/* }}} */

/* {{{ proto string zredis_get_last_error()
   Get last error string. */
PHP_FUNCTION(zredis_get_last_error) {
    zval* zobj;
    zredis_t* client;
    if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "O", &zobj, zredis_ce) == FAILURE) {
        RETURN_FALSE;
    }
    client = Z_ZREDIS_P(zobj);
    if (client->err) {
        #if PHP_MAJOR_VERSION >= 7
            RETURN_STRING(client->errstr);
        #else
            RETURN_STRING(client->errstr, 1);
        #endif
    }
    RETURN_NULL();
}
/* }}} */

/* {{{ zredis_methods */
zend_function_entry zredis_methods[] = {
    PHP_ME(zredis, __construct, arginfo_zredis_none, ZEND_ACC_CTOR | ZEND_ACC_PUBLIC)
    PHP_ME(zredis, __destruct,  arginfo_zredis_none, ZEND_ACC_PUBLIC)
    PHP_ME(zredis, __call,      arginfo_zredis_call, ZEND_ACC_PUBLIC)
    PHP_ME_MAPPING(connect,              zredis_connect,              arginfo_zredis_connect,              ZEND_ACC_PUBLIC)
    PHP_ME_MAPPING(pconnect,             zredis_pconnect,             arginfo_zredis_pconnect,             ZEND_ACC_PUBLIC)
    PHP_ME_MAPPING(setTimeout,           zredis_set_timeout,          arginfo_zredis_set_timeout,          ZEND_ACC_PUBLIC)
    PHP_ME_MAPPING(getTimeout,           zredis_get_timeout,          arginfo_zredis_none,                 ZEND_ACC_PUBLIC)
    PHP_ME_MAPPING(setKeepAliveInterval, zredis_set_keep_alive_int,   arginfo_zredis_set_keep_alive_int,   ZEND_ACC_PUBLIC)
    PHP_ME_MAPPING(getKeepAliveInterval, zredis_get_keep_alive_int,   arginfo_zredis_none,                 ZEND_ACC_PUBLIC)
    PHP_ME_MAPPING(setMaxReadBuf,        zredis_set_max_read_buf,     arginfo_zredis_set_max_read_buf,     ZEND_ACC_PUBLIC)
    PHP_ME_MAPPING(getMaxReadBuf,        zredis_get_max_read_buf,     arginfo_zredis_none,                 ZEND_ACC_PUBLIC)
    PHP_ME_MAPPING(setThrowExceptions,   zredis_set_throw_exceptions, arginfo_zredis_set_throw_exceptions, ZEND_ACC_PUBLIC)
    PHP_ME_MAPPING(getThrowExceptions,   zredis_get_throw_exceptions, arginfo_zredis_none,                 ZEND_ACC_PUBLIC)
    PHP_ME_MAPPING(sendRaw,              zredis_send_raw,             arginfo_zredis_send_raw,             ZEND_ACC_PUBLIC)
    PHP_ME_MAPPING(sendRawArray,         zredis_send_raw_array,       arginfo_zredis_send_raw_array,       ZEND_ACC_PUBLIC)
    PHP_ME_MAPPING(appendRaw,            zredis_append_command,       arginfo_zredis_send_raw,             ZEND_ACC_PUBLIC)
    PHP_ME_MAPPING(appendRawArray,       zredis_append_command_array, arginfo_zredis_send_raw_array,       ZEND_ACC_PUBLIC)
    PHP_ME_MAPPING(getReply,             zredis_get_reply,            arginfo_zredis_none,                 ZEND_ACC_PUBLIC)
    PHP_ME_MAPPING(getLastError,         zredis_get_last_error,       arginfo_zredis_none,                 ZEND_ACC_PUBLIC)
#ifdef HAVE_ZREDIS_RECONNECT
    PHP_ME_MAPPING(reconnect,            zredis_reconnect,            arginfo_zredis_none,                 ZEND_ACC_PUBLIC)
#endif
    PHP_FE_END
};
/* }}} */

/* {{{ PHP_MINFO_FUNCTION */
PHP_MINFO_FUNCTION(zredis) {
    char zredis_version[32];
    snprintf(zredis_version, sizeof(zredis_version), "%d.%d.%d", HIREDIS_MAJOR, HIREDIS_MINOR, HIREDIS_PATCH);
    php_info_print_table_start();
    php_info_print_table_header(2, "zredis support", "enabled");
    php_info_print_table_row(2, "zredis module version", PHP_ZREDIS_VERSION);
    php_info_print_table_row(2, "zredis version", zredis_version);
    php_info_print_table_row(2, "author", "Adam, shenzhe");
    php_info_print_table_end();
    DISPLAY_INI_ENTRIES();
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION */
PHP_MINIT_FUNCTION(zredis) {
    zend_class_entry ce;

    // Register zredis class
    INIT_CLASS_ENTRY(ce, "zredis", zredis_methods);
    #if PHP_MAJOR_VERSION >= 7
        zredis_ce = zend_register_internal_class(&ce);
        zredis_ce->create_object = zredis_obj_new;
    #else
        ce.create_object = zredis_obj_new;
        zredis_ce = zend_register_internal_class(&ce TSRMLS_CC);
    #endif
    memcpy(&zredis_obj_handlers, zend_get_std_object_handlers(), sizeof(zredis_obj_handlers));
    #if PHP_MAJOR_VERSION >= 7
        zredis_obj_handlers.offset = XtOffsetOf(zredis_t, std);
        zredis_obj_handlers.free_obj = zredis_obj_free;
    #endif

    // Register zredisException class
    INIT_CLASS_ENTRY(ce, "zredisException", NULL);
    #if PHP_MAJOR_VERSION >= 7
        zredis_exception_ce = zend_register_internal_class_ex(&ce, zend_ce_exception);
    #else
        zredis_exception_ce = zend_register_internal_class_ex(&ce, zend_exception_get_default(TSRMLS_C), NULL TSRMLS_CC);
    #endif
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION */
PHP_MSHUTDOWN_FUNCTION(zredis) {
    if(persisent) {
        if (zredis_client->ctx) {
            redisFree(zredis_client->ctx);
        }
        zredis_client->ctx = NULL;
    }
    return SUCCESS;
}
/* }}} */

/* {{{ zredis_module_entry */
zend_module_entry zredis_module_entry = {
    STANDARD_MODULE_HEADER,
    "zredis",
    NULL,
    PHP_MINIT(zredis),
    PHP_MSHUTDOWN(zredis),
    NULL,
    NULL,
    PHP_MINFO(zredis),
    PHP_ZREDIS_VERSION,
    STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_ZREDIS
ZEND_GET_MODULE(zredis)
#endif

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
