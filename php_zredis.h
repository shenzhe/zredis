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

#ifndef PHP_ZREDIS_H
#define PHP_ZREDIS_H

#define PHP_ZREDIS_VERSION "0.1"

#include <hiredis.h>

typedef struct {
#if PHP_MAJOR_VERSION < 7
    zend_object std;
#endif
    redisContext* ctx;
    long timeout_us;
    int keep_alive_int_s;
    size_t max_read_buf;
    int throw_exceptions;
    int err;
    int persisent;
    char errstr[128];
#if PHP_MAJOR_VERSION >= 7
    zend_object std;
#endif
} zredis_t;

extern zend_module_entry zredis_module_entry;
#define phpext_zredis_ptr &zredis_module_entry

#endif

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
