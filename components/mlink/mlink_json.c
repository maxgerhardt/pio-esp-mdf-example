// Copyright 2017 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cJSON.h"
#include "mlink_json.h"

static const char *TAG = "mlink_json";

esp_err_t __mlink_json_parse(const char *json_str, const char *key,
                             void *value, int value_type)
{
    MDF_PARAM_CHECK(json_str);
    MDF_PARAM_CHECK(key);
    MDF_PARAM_CHECK(value);

    MDF_LOGV("value_type: %d", value_type);

    cJSON *pJson = cJSON_Parse(json_str);
    MDF_ERROR_CHECK(!pJson, ESP_FAIL, "cJSON_Parse, json_str: %s, key: %s", json_str, key);

    cJSON *pSub = cJSON_GetObjectItem(pJson, key);

    if (!pSub) {
        MDF_LOGV("cJSON_GetObjectItem, json_str: %s, key: %s", json_str, key);
        goto ERR_EXIT;
    }

    char *pSub_raw     = NULL;
    char **array_index = NULL;
    int array_size     = 0;

    if (pSub->type == cJSON_Array) {
        pSub->valueint = cJSON_GetArraySize(pSub);
    }

    switch (value_type) {
        case MLINK_JSON_TYPE_INT8:
            *((char *)value) = pSub->valueint;
            break;

        case MLINK_JSON_TYPE_INT16:
            *((short *)value) = pSub->valueint;
            break;

        case MLINK_JSON_TYPE_INT32:
            *((int *)value) = pSub->valueint;
            break;

        case MLINK_JSON_TYPE_FLOAT:
            *((float *)value) = (float)(pSub->valuedouble);
            break;

        case MLINK_JSON_TYPE_DOUBLE:
            *((double *)value) = (double)pSub->valuedouble;
            break;

        /**< "case 6" means string, bur it should be handle in default channel,
             because it may just want to query string or subJson raw */
        default:

            MDF_LOGV("pSub->type: %d", pSub->type);

            switch (pSub->type) {
                case cJSON_False:
                    *((char *)value) = false;
                    break;

                case cJSON_True:
                    *((char *)value) = true;
                    break;

                case cJSON_Number:
                    if (!pSub->valueint) {
                        goto ERR_EXIT;
                    }

                    *((char *)value) = pSub->valueint;
                    break;

                case cJSON_String:
                    if (value_type == MLINK_JSON_TYPE_POINTER) {
                        *((char **)value) = MDF_REALLOC_RETRY(NULL, strlen(pSub->valuestring) + 1);
                        memcpy(*((char **)value), pSub->valuestring, strlen(pSub->valuestring) + 1);
                    } else {
                        memcpy(value, pSub->valuestring, strlen(pSub->valuestring) + 1);
                    }

                    break;

                case cJSON_Object:
                    pSub_raw = cJSON_PrintUnformatted(pSub);
                    MDF_ERROR_GOTO(!pSub_raw, ERR_EXIT, "cJSON_PrintUnformatted");

                    if (value_type == MLINK_JSON_TYPE_POINTER) {
                        *((char **)value) = pSub_raw;
                    } else {
                        memcpy(value, pSub_raw, strlen(pSub_raw) + 1);
                        MDF_FREE(pSub_raw);
                    }

                    break;

                case cJSON_Array:
                    array_size = cJSON_GetArraySize(pSub);
                    array_index = (char **)value;

                    for (int i = 0; i < array_size; ++i) {
                        cJSON *item = cJSON_GetArrayItem(pSub, i);

                        if (item->type == cJSON_Number) {
                            *((int *)value + i) = cJSON_GetArrayItem(pSub, i)->valueint;
                            continue;
                        }

                        if (item->type == cJSON_String) {
                            *array_index = MDF_REALLOC_RETRY(NULL, strlen(item->valuestring) + 1);
                            strcpy(*array_index, item->valuestring);
                            (*array_index)[strlen(item->valuestring)] = '\0';
                            array_index++;
                            continue;
                        }

                        if (item->type == cJSON_Object) {
                            MDF_ERROR_GOTO(!(*array_index++ = cJSON_PrintUnformatted(item)),
                                           ERR_EXIT, "cJSON_PrintUnformatted NULL");
                        }

                        /**< no sub cJSON_Array, just support one layer of cJSON_Array */
                    }

                    break;

                default:
                    MDF_LOGE("does not support this type(%d) of data parsing", pSub->type);
                    break;
            }
    }

    cJSON_Delete(pJson);
    return ESP_OK;

ERR_EXIT:
    cJSON_Delete(pJson);
    return ESP_FAIL;
}

ssize_t __mlink_json_pack(char **json_ptr, const char *key, int value, int value_type)
{
    MDF_PARAM_CHECK(key);
    MDF_PARAM_CHECK(json_ptr);

    char *json_str = (char *)json_ptr;

    MDF_LOGV("key: %s, value: %d, value_type: %d", key, value, value_type);

    if (value_type / MLINK_JSON_TYPE_POINTER) {
        value_type %= MLINK_JSON_TYPE_POINTER;
        MDF_ERROR_CHECK(value_type == MLINK_JSON_TYPE_STRING && !value,
                        MDF_ERR_INVALID_ARG, "<MDF_ERR_INVALID_ARG> !(value)");

        size_t value_len = (value_type == MLINK_JSON_TYPE_STRING) ? strlen((char *)value) : 10;

        if (!*json_ptr) {
            *json_ptr = MDF_CALLOC(1, value_len + strlen(key) + 16);
        } else {
            *json_ptr = MDF_REALLOC_RETRY(*json_ptr, value_len + strlen(key) + 16 + strlen(*json_ptr));
        }

        json_str = *json_ptr;
    }


    /**< start symbol of a json object */
    char identifier = '{';

    /**< pack data into array that has been existed */
    if (*key == '[') {
        identifier = '[';
        key = NULL;
    }

    int index = 0;

    if (*json_str == identifier) {
        index     = strlen(json_str) - 1;
        json_str += index;
        *json_str = ',';
    } else {
        *json_str = identifier;
    }

    /**< forward to key field by "++" */
    json_str++;
    index++;

    /**< pack key into json_str */
    int tmp_len = key ? sprintf(json_str, "\"%s\":", key) : 0;
    json_str += tmp_len;
    index    += tmp_len;

    switch (value_type) {
        case MLINK_JSON_TYPE_INT8: /**< integral number */
        case MLINK_JSON_TYPE_INT16: /**< integral number */
        case MLINK_JSON_TYPE_INT32: /**< integral number */
            tmp_len = sprintf(json_str, "%d", value);
            break;

        case MLINK_JSON_TYPE_STRING: /**< string */
            if (*((char *)value) == '{' || *((char *)value) == '[') {
                tmp_len = sprintf(json_str, "%s", (char *)value);
            } else {
                tmp_len = sprintf(json_str, "\"%s\"", (char *)value);
            }

            break;

        default:
            MDF_LOGE("key: %s, invalid type: %d", key, value_type);
            return ESP_FAIL;
    }

    /**< update json_str and index after packed key */
    json_str += tmp_len;
    index    += tmp_len;

    /**< finish json_str with '}' or ']' */
    *json_str = identifier == '{' ? '}' : ']';
    json_str++;
    *json_str = '\0';

    index++;

    return index;
}

ssize_t mlink_json_pack_double(char **json_ptr, const char *key, double value)
{
    MDF_PARAM_CHECK(key);
    MDF_PARAM_CHECK(json_ptr);

    if (!*json_ptr) {
        *json_ptr = MDF_REALLOC_RETRY(NULL, strlen(key) + 32);
        memset(*json_ptr, 0, strlen(key) + 32);
    } else {
        *json_ptr = MDF_REALLOC_RETRY(*json_ptr, strlen(key) + 32 + strlen(*json_ptr));
    }

    char *json_str = *json_ptr;
    /**< start symbol of a json object */
    char identifier = '{';

    /**< pack data into array that has been existed */
    if (*key == '[') {
        identifier = '[';
        key = NULL;
    }

    int index = 0;

    if (*json_str == identifier) {
        index     = strlen(json_str) - 1;
        json_str += index;
        *json_str = ',';
    } else {
        *json_str = identifier;
    }

    /**< forward to key field by "++" */
    json_str++;
    index++;

    /**< pack key into json_str */
    int tmp_len = key ? sprintf(json_str, "\"%s\":", key) : 0;
    json_str += tmp_len;
    index    += tmp_len;
    tmp_len = sprintf(json_str, "%lf", value);

    /**< update json_str and index after packed key */
    json_str += tmp_len;
    index    += tmp_len;

    /**< finish json_str with '}' or ']' */
    *json_str = identifier == '{' ? '}' : ']';
    json_str++;
    *json_str = '\0';

    index++;

    return index;
}
