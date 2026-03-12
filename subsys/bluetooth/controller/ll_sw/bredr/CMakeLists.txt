# /****************************************************************************
#  *  Copyright (C) 2026 Xiaomi Corporation
#  *
#  * Licensed under the Apache License, Version 2.0 (the "License");
#  * you may not use this file except in compliance with the License.
#  * You may obtain a copy of the License at
#  *
#  *      http://www.apache.org/licenses/LICENSE-2.0
#  *
#  * Unless required by applicable law or agreed to in writing, software
#  * distributed under the License is distributed on an "AS IS" BASIS,
#  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  * See the License for the specific language governing permissions and
#  * limitations under the License.
#  ***************************************************************************/

# BR/EDR Controller CMakeLists.txt

if(CONFIG_BT_CTLR_BREDR)

zephyr_library()

# Core BR/EDR Controller sources
zephyr_library_sources(
  radio_bredr.c
  lll_bredr.c
  ull_bredr.c
  lmp_proc.c
  hci_bredr.c
  ticker_bredr.c
)

zephyr_library_include_directories(
  ${ZEPHYR_BASE}/subsys/bluetooth/controller
  ${ZEPHYR_BASE}/subsys/bluetooth/controller/include
  ${ZEPHYR_BASE}/subsys/bluetooth/controller/ll_sw
  ${ZEPHYR_BASE}/subsys/bluetooth/controller/ll_sw/bredr
  ${ZEPHYR_BASE}/subsys/bluetooth/controller/hal
  ${ZEPHYR_BASE}/subsys/bluetooth/controller/util
  ${ZEPHYR_BASE}/subsys/bluetooth/controller/ticker
)

endif() # CONFIG_BT_CTLR_BREDR
