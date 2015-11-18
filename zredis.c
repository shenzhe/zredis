/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2015 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author:                                    shenzhe163@gmail.com                      |
  +----------------------------------------------------------------------+
 */

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_zredis.h"

zend_class_entry *zredis_ce;

ZEND_BEGIN_ARG_INFO_EX(arginfo_zredis_none, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_zredis_call, 0, 0, 2)
    ZEND_ARG_INFO(0, func_name)
    ZEND_ARG_INFO(0, func_args)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_zredis_connect, 0, 0, 1)
    ZEND_ARG_INFO(0, ip)
    ZEND_ARG_INFO(0, port)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_zredis_cmd, 0, 0, 1)
    ZEND_ARG_INFO(0, command_args)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_zredis_multi, 0, 0, 1)
    ZEND_ARG_INFO(0, command_args)
ZEND_END_ARG_INFO()

static inline zredis_obj* zredis_fetch(zend_object* obj) {
    return (zredis_obj*)((char*)(obj) - XtOffsetOf(zredis_obj, std));
}

static inline zend_object* zredis_new(zend_class_entry *ce) {
    zredis_obj* client;
    client = ecalloc(1, sizeof(zredis_obj) + zend_object_properties_size(ce));
    zend_object_std_init(&client->std, ce);
    object_properties_init(&client->std, ce);
    client->std.handlers = &zredis_obj_handlers;
    return &client->std;
}
static void zredis_free(zend_object *object) {
    zredis_obj* client;
    client = zredis_fetch(object);
    if (!client) {
        return;
    }
    if (client->rctx) {
        redisFree(client->rctx);
    }
    zend_object_std_dtor(&client->std);
    efree(client);
}

static void _to_array(zval* arr, zval** ret_zvals, int* ret_num_zvals) {
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

    ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(arr), zv) {
        ZVAL_COPY_VALUE(&zvals[i], zv);
        i++;
    } ZEND_HASH_FOREACH_END();

    *ret_zvals = zvals;
    *ret_num_zvals = argc;
}

static void _zredis_cmd_array(INTERNAL_FUNCTION_PARAMETERS, zredis_obj* client, char* cmd, zval* args, int argc, int is_append) {
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
            Z_TYPE_P(_zp) == IS_TRUE || Z_TYPE_P(_zp) == IS_FALSE
        ) {
            string_args[j] = zend_is_true(_zp) ? "1" : "0";
            string_lens[j] = 1;
        } else {
            if (Z_TYPE_P(_zp) != IS_STRING) {
                convert_to_string_ex(_zp);
                string_zstrs[j] = Z_STR_P(_zp);
            }
            string_args[j] = Z_STRVAL_P(_zp);
            string_lens[j] = Z_STRLEN_P(_zp);
        }
    }

    if (is_append) {
        if (REDIS_OK != redisAppendCommandArgv(client->rctx, num_strings, (const char**)string_args, string_lens)) {
            RETVAL_FALSE;
        } else {
            RETVAL_TRUE;
        }
    } else {
        redisReplyReaderSetPrivdata(client->rctx->reader, (void*)return_value);
        if (zv = (zval*)redisCommandArgv(client->rctx, num_strings, (const char**)string_args, string_lens)) {
            assert(zv == return_value);
            RETVAL_ZVAL(return_value, 1, 1)
        } else {
            RETVAL_FALSE;
        }
    }

    // Cleanup
    for (i = 0; i < num_strings; i++) {
        if (string_zstrs[i]) {
            zend_string_release(string_zstrs[i]);
        }
    }
    efree(string_zstrs);
    efree(string_args);
    efree(string_lens);
}

static void _zredis_cmd(INTERNAL_FUNCTION_PARAMETERS, int is_array, int is_append) {
    zredis_obj* client;
    zval* zobj;
    int argc;
    zval* args;
    zval* varargs;

    if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "O+", &zobj, zredis_ce, &varargs, &argc) == FAILURE) {
        RETURN_FALSE;
    }
    if ((is_array && argc != 1) || (!is_array && argc < 1)) {
        WRONG_PARAM_COUNT;
    }

    client = zredis_fetch(Z_OBJ_P(zobj));

    if (is_array) {
        _to_array(varargs, &args, &argc);
    } else {
        args = varargs;
    }

    _zredis_cmd_array(INTERNAL_FUNCTION_PARAM_PASSTHRU, client, NULL, args, argc, is_append);

    if (is_array) efree(args);
}

static zval* _zredis_replyobj_nest(const redisReadTask* task, zval* z) {
    zval* rv = z;
    if (task->parent) {
        zval* parent;
        parent = (zval*)task->parent->obj;
        assert(Z_TYPE_P(parent) == IS_ARRAY);
        rv = zend_hash_index_update(Z_ARRVAL_P(parent), task->idx, z);
    }
    return rv;
}

/* redisReplyObjectFunctions: Get zval to operate on */
static zval* _zredis_replyobj_get_zval(const redisReadTask* task, zval* stack_zval) {
    zval* rv;
    if (task->parent) {
        rv = stack_zval;
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
        
    } else {
        ZVAL_STRINGL(z, str, len);
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

static void zredis_replyobj_free(void* obj) {
}

static redisReplyObjectFunctions zredis_replyobj_funcs = {
    zredis_replyobj_create_string,
    zredis_replyobj_create_array,
    zredis_replyobj_create_integer,
    zredis_replyobj_create_nil,
    zredis_replyobj_free
};

static int _zredis_conn_init(zredis_obj* client) {
    int rc;
    rc = REDIS_OK;
    client->rctx->reader->maxbuf = REDIS_READER_MAX_BUF;
    client->rctx->reader->fn = &zredis_replyobj_funcs;
    return rc;
}

PHP_METHOD(zredis, __construct) {
    zredis_obj* client;
    if (zend_parse_parameters_none() == FAILURE) {
        return;
    }
    return_value = getThis();
    client = zredis_fetch(Z_OBJ_P(return_value))
    client->rctx = NULL;
}

PHP_METHOD(zredis, __destruct) {
    zredis_obj* client;
    return_value = getThis();
    client = zredis_fetch(Z_OBJ_P(return_value))
    if (client->rctx) {
        redisFree(client->rctx);
    }
    client->rctx = NULL;
}

PHP_METHOD(zredis, __call) {
    zredis_obj* client;
    char* ofunc;
    char* func;
    strlen_t func_len;
    char* cmd;
    zval* func_args;
    int func_argc;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "sa", &ofunc, &func_len, &func_args) == FAILURE) {
        RETURN_FALSE;
    }

    client = zredis_fetch(Z_OBJ_P(getThis()))

    func = estrndup(ofunc, func_len);
    php_strtoupper(func, func_len);

    _to_array(func_args, &func_args, &func_argc);
    _zredis_cmd_array(INTERNAL_FUNCTION_PARAM_PASSTHRU, client, func, func_args, func_argc, 0);
    efree(func_args);
    efree(func);
}

PHP_FUNCTION(zredis_connect) {
    zval* zobj;
    zredis_obj* client;
    char* ip = "127.0.0.1";
    strlen_t ip_len = 10;
    long port = 6379;
    if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "O|sl", &zobj, zredis_ce, &ip, &ip_len, &port) == FAILURE) {
        RETURN_FALSE;
    }
    client = zredis_fetch(Z_OBJ_P(zobj));
    if (client->rctx) {
        redisFree(client->rctx);
    }
    client->rctx = NULL;
    if ('/' == ip[0]) {
        client->rctx = redisConnectUnix(ip);
    } else {
        client->rctx = redisConnect(ip, port);
    }
    if (!client->rctx) {
        RETURN_FALSE;
    } else if (client->rctx->err) {
        RETURN_FALSE;
    }
    if (REDIS_OK == _zredis_conn_init(client)) {
        RETURN_TRUE;
    }
    RETURN_FALSE;
}

PHP_FUNCTION(zredis_cmd) {
    _zredis_cmd(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0, 0);
}

PHP_FUNCTION(zredis_multi) {
    _zredis_cmd(INTERNAL_FUNCTION_PARAM_PASSTHRU, 1, 0);
}

PHP_FUNCTION(zredis_reply) {
    zval* zobj;
    zredis_obj* client;
    zval* reply = NULL;
    if (zend_parse_method_parameters(ZEND_NUM_ARGS(), getThis(), "O", &zobj, zredis_ce) == FAILURE) {
        RETURN_FALSE;
    }
    client = zredis_fetch(Z_OBJ_P(zobj));
    redisReplyReaderSetPrivdata(client->rctx->reader, (void*)return_value);
    if (REDIS_OK != redisGetReply(client->rctx, (void**)&reply)) {
        RETURN_FALSE;
    } else if (reply) {
        assert(reply == return_value);
        RETVAL_ZVAL(return_value, 1, 1)
    } else {
        RETURN_FALSE;
    }
}

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(zredis) {
    zend_class_entry ce;
    INIT_CLASS_ENTRY(ce, "zredis",zredis_methods);
    zredis_ce = zend_register_internal_class(&ce);
    zredis_ce->create_object = zredis_new
    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(zredis) {
    /* uncomment this line if you have INI entries
    UNREGISTER_INI_ENTRIES();
     */
    return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request start */

/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(zredis) {
#if defined(COMPILE_DL_ZREDIS) && defined(ZTS)
    ZEND_TSRMLS_CACHE_UPDATE();
#endif
    return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request end */

/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(zredis) {
    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(zredis) {
    php_info_print_table_start();
    php_info_print_table_header(2, "zredis support", "enabled");
    php_info_print_table_end();

    /* Remove comments if you have entries in php.ini
    DISPLAY_INI_ENTRIES();
     */
}
/* }}} */

/* {{{ zredis_functions[]
 *
 * Every user visible function must have an entry in zredis_functions[].
 */
const zend_function_entry zredis_methods[] = {
    PHP_ME(zredis, __construct, arginfo_zredis_none, ZEND_ACC_CTOR | ZEND_ACC_PUBLIC)  //
    PHP_ME(zredis, __destruct,  arginfo_zredis_none, ZEND_ACC_PUBLIC)
    PHP_ME(zredis, __call,      arginfo_zredis_call, ZEND_ACC_PUBLIC)
    PHP_ME(zredis, connect,     arginfo_zredis_connect, ZEND_ACC_PUBLIC)
    PHP_ME(zredis, cmd,         arginfo_zredis_cmd, ZEND_ACC_PUBLIC)
    PHP_ME(zredis, multi,       arginfo_zredis_multi, ZEND_ACC_PUBLIC)
    PHP_FE_END /* Must be the last line in zredis_functions[] */
};
/* }}} */

/* {{{ zredis_module_entry
 */
zend_module_entry zredis_module_entry = {
    STANDARD_MODULE_HEADER,
    "zredis",
    zredis_functions,
    PHP_MINIT(zredis),
    PHP_MSHUTDOWN(zredis),
    PHP_RINIT(zredis), /* Replace with NULL if there's nothing to do at request start */
    PHP_RSHUTDOWN(zredis), /* Replace with NULL if there's nothing to do at request end */
    PHP_MINFO(zredis),
    PHP_ZREDIS_VERSION,
    STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_ZREDIS
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE();
#endif
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
