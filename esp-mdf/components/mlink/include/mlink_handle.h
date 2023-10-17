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

#ifndef __MLINK_HANDLE_H__
#define __MLINK_HANDLE_H__

#include "mdf_common.h"
#include "mlink_json.h"
#include "mlink_utils.h"
#include "mlink_notice.h"

#ifdef __cplusplus
extern "C" {
#endif /**< _cplusplus */

/**
 * @brief Permissions for the characteristics
 */
typedef enum {
    CHARACTERISTIC_PERMS_READ    = 1 << 0, /**< The characteristic of the device are readable */
    CHARACTERISTIC_PERMS_WRITE   = 1 << 1, /**< The characteristic of the device are writable*/
    CHARACTERISTIC_PERMS_TRIGGER = 1 << 2, /**< The characteristic of the device can be triggered */
    CHARACTERISTIC_PERMS_RW      = CHARACTERISTIC_PERMS_READ | CHARACTERISTIC_PERMS_WRITE,    /**< The characteristic of the device are readable & writable */
    CHARACTERISTIC_PERMS_RT      = CHARACTERISTIC_PERMS_READ | CHARACTERISTIC_PERMS_TRIGGER,  /**< The characteristic of the device are readable & triggered */
    CHARACTERISTIC_PERMS_WT      = CHARACTERISTIC_PERMS_WRITE | CHARACTERISTIC_PERMS_TRIGGER, /**< The characteristic of the device are writable & triggered */
    CHARACTERISTIC_PERMS_RWT     = CHARACTERISTIC_PERMS_RW | CHARACTERISTIC_PERMS_TRIGGER,    /**< The characteristic of the device are readable & writable & triggered */
} characteristic_perms_t;

/**
 * @brief Format for the characteristic
 */
typedef enum {
    CHARACTERISTIC_FORMAT_NONE,   /**< Invalid format */
    CHARACTERISTIC_FORMAT_INT,    /**< characteristic is a number format */
    CHARACTERISTIC_FORMAT_DOUBLE, /**< characteristic is a double format */
    CHARACTERISTIC_FORMAT_STRING, /**< characteristic is a string format */
} characteristic_format_t;

/**
 * @brief The data type of the parameter of the handler
 */
typedef struct {
    const char *req_data;      /**< Received request data */
    ssize_t req_size;          /**< The length of the received request data */
    mlink_httpd_format_t req_fromat; /**< The format of the received request data */
    char *resp_data;           /**< Response data to be sent */
    ssize_t resp_size;         /**< The length of response data to be sent */
    mlink_httpd_format_t resp_fromat; /**< The format of response data to be sent */
} mlink_handle_data_t;

/**
 * @brief Type of request handler
 */
typedef mdf_err_t (*mlink_handle_func_t)(mlink_handle_data_t *data);

/**
 * @brief Get the type of callback function that sets the characteristic value
 */
typedef mdf_err_t (*mlink_characteristic_func_t)(uint16_t cid, void *value);

/**
 * @brief Configuring basic information about the device
 *
 * @param  tid     Unique identifier for the device type
 * @param  name    The name of device
 * @param  version The version of device
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
mdf_err_t mlink_add_device(uint32_t tid, const char *name, const char *version);

/**
 * @brief  Set device name
 *
 * @param name The name of device
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
mdf_err_t mlink_device_set_name(const char *name);

/**
 * @brief Set device version
 *
 * @param position The position of device
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
mdf_err_t mlink_device_set_position(const char *position);

/**
 * @brief  Get device name
 *
 * @return Name of the device
 */
const char *mlink_device_get_name();

/**
 * @brief  Get device version
 *
 * @return Version of the device
 */
const char *mlink_device_get_position();

/**
 * @brief  Get device tid
 *
 * @return Unique identifier for the device type
 */
int mlink_device_get_tid();

/**
 * @brief Add device characteristic information
 *
 * @param  cid    Unique identifier for the characteristic
 * @param  name   The name of the characteristic
 * @param  format The format of the characteristic
 * @param  perms  The permissions of the characteristic
 * @param  min    The min of the characteristic
 * @param  max    The max of the characteristic
 * @param  step   The step of the characteristic
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
mdf_err_t mlink_add_characteristic(uint16_t cid, const char *name, characteristic_format_t format,
                                   characteristic_perms_t perms, int min, int max, uint16_t step);

/**
 * @brief Increase the device's characteristic handler
 *
 * @param  get_value_func [description]
 * @param  set_value_func [description]
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
mdf_err_t mlink_add_characteristic_handle(mlink_characteristic_func_t get_value_func,
        mlink_characteristic_func_t set_value_func);

/**
 * @brief Handling requests from the APP
 *
 * @param  src_addr Source address of the device
 * @param  type     Type of data
 * @param  data     Requested message
 * @param  size     The length of the requested data
 *
 * @note This function is deprecated. Use 'mlink_handle_request' function
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
mdf_err_t mlink_handle(const uint8_t *src_addr, const mlink_httpd_type_t *type,
                       const void *data, size_t size) __attribute__((deprecated));

/**
 * @brief Add or modify a request handler
 *
 * @param  name The name of the handler
 * @param  func The pointer of the handler
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
mdf_err_t mlink_set_handle(const char *name, const mlink_handle_func_t func);


/**
 * @brief Call the handler in the request list
 *
 * @param handle_data The data type of the parameter of the handler
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 *     - MDF_ERR_NOT_SUPPORTED
 */
mdf_err_t mlink_handle_request(mlink_handle_data_t *handle_data);

#ifdef __cplusplus
}
#endif /**< _cplusplus */

#endif /**< __MLINK_HANDLE_H__ */
