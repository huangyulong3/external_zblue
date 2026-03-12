# Zephyr BR/EDR Controller 详细设计文档

## 1. 概述

本文档描述了基于Zephyr RTOS的BR/EDR Controller实现，遵循Bluetooth Core Specification v6.1 Vol 2规范�?

### 1.1 设计目标

- 与现有Zephyr BLE Controller架构保持一�?
- 复用ticker调度机制实现BLE/BR/EDR共存
- 支持完整的BR/EDR功能：Inquiry、Page、ACL、SCO/eSCO

### 1.2 规范参�?

- Bluetooth Core Spec Vol 2, Part B: Baseband Specification
- Bluetooth Core Spec Vol 2, Part C: Link Manager Protocol
- Bluetooth Core Spec Vol 4, Part E: HCI (BR/EDR部分)

---

## 2. 整体架构

### 2.1 分层架构�?

```
┌─────────────────────────────────────────────────────────────────────────────�?
�?                             Host Stack                                      �?
�?                        (subsys/bluetooth/host)                             �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?HCI Commands/Events
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?                          HCI Layer (hci_bredr.c)                           �?
�? ┌─────────────────�? ┌─────────────────�? ┌─────────────────────────────�?�?
�? �?Command Handler �? �?Event Generator �? �?ACL Data Handler            �?�?
�? �?hci_bredr_cmd_  �? �?hci_bredr_evt_  �? �?hci_bredr_acl_data_tx/rx    �?�?
�? �?handle()        �? �?xxx()           �? �?                            �?�?
�? └────────┬────────�? └────────▲────────�? └──────────┬──────────────────�?�?
└───────────┼────────────────────┼─────────────────────┼──────────────────────�?
            �?                   �?                    �?
            �?                   �?                    �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?                     Upper Link Layer (ull_bredr.c)                         �?
�? ┌──────────────────────────────────────────────────────────────────────�? �?
�? �?                   Link Manager Environment (LM_ENV)                  �? �?
�? �? ┌─────────────�?┌─────────────�?┌─────────────�?┌───────────────�? �? �?
�? �? �?HCI Config  �?�?Connection  �?�?AFH Global  �?�?SAM Global    �? �? �?
�? �? �?Parameters  �?�?Info Pool   �?�?Parameters  �?�?Parameters    �? �? �?
�? �? └─────────────�?└─────────────�?└─────────────�?└───────────────�? �? �?
�? └──────────────────────────────────────────────────────────────────────�? �?
�? ┌──────────────────────────────────────────────────────────────────────�? �?
�? �?                   Connection Context Pool                            �? �?
�? �? ┌─────────────────────────────────────────────────────────────────�?�? �?
�? �? �?ull_bredr_conn[0..MAX_CONN-1]                                   �?�? �?
�? �? �? ├─ lc_state (Link Controller State Machine)                    �?�? �?
�? �? �? ├─ link (QoS, packet types, poll interval)                     �?�? �?
�? �? �? ├─ info (remote features, version, name)                       �?�? �?
�? �? �? ├─ enc (encryption keys, mode, state)                          �?�? �?
�? �? �? ├─ req (pending local/peer requests)                           �?�? �?
�? �? �? ├─ afh (channel map, reporting)                                �?�? �?
�? �? �? ├─ sp (SSP state, keys, commitments)                           �?�? �?
�? �? �? └─ lmp_tx_pending / lmp_rx_pending (transaction queues)        �?�? �?
�? �? └─────────────────────────────────────────────────────────────────�?�? �?
�? └──────────────────────────────────────────────────────────────────────�? �?
�? ┌────────────────�? ┌────────────────�? ┌────────────────────────────�?  �?
�? �?LMP Procedures �? �?State Machine  �? �?Collision/Conflict Manager �?  �?
�? �?(lmp_proc.c)   �? �?Handler        �? �?                           �?  �?
�? └───────┬────────�? └───────┬────────�? └─────────────┬──────────────�?  �?
└──────────┼───────────────────┼─────────────────────────┼────────────────────�?
           �?                  �?                        �?
           �?                  �?                        �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?                   Ticker Integration (ticker_bredr.c)                       �?
�? ┌──────────────────────────────────────────────────────────────────────�? �?
�? �?                   Coexistence Scheduler                              �? �?
�? �? ┌─────────────────�? ┌─────────────────�? ┌─────────────────────�? �? �?
�? �? �?BLE/BR/EDR      �? �?Priority        �? �?Time Slice          �? �? �?
�? �? �?Arbitration     �? �?Management      �? �?Computation         �? �? �?
�? �? └─────────────────�? └─────────────────�? └─────────────────────�? �? �?
�? └──────────────────────────────────────────────────────────────────────�? �?
�? ┌──────────────────────────────────────────────────────────────────────�? �?
�? �?                   Ticker Nodes                                       �? �?
�? �? INQUIRY | INQ_SCAN | PAGE | PAGE_SCAN | CONN[0..n] | SCO | eSCO     �? �?
�? └──────────────────────────────────────────────────────────────────────�? �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?Ticker Callbacks
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?                     Lower Link Layer (lll_bredr.c)                         �?
�? ┌──────────────────────────────────────────────────────────────────────�? �?
�? �?                   Radio Event Handlers                               �? �?
�? �? ┌─────────────────�? ┌─────────────────�? ┌─────────────────────�? �? �?
�? �? �?Prepare Handler �? �?ISR Handler     �? �?Done Handler        �? �? �?
�? �? �?lll_bredr_xxx_  �? �?lll_bredr_xxx_  �? �?lll_bredr_xxx_      �? �? �?
�? �? �?prepare()       �? �?isr()           �? �?done()              �? �? �?
�? �? └─────────────────�? └─────────────────�? └─────────────────────�? �? �?
�? └──────────────────────────────────────────────────────────────────────�? �?
�? ┌──────────────────────────────────────────────────────────────────────�? �?
�? �?                   Baseband Functions                                 �? �?
�? �? ┌─────────────────�? ┌─────────────────�? ┌─────────────────────�? �? �?
�? �? �?Frequency       �? �?Access Code     �? �?Whitening/CRC       �? �? �?
�? �? �?Hopping         �? �?Generation      �? �?E0 Encryption       �? �? �?
�? �? └─────────────────�? └─────────────────�? └─────────────────────�? �? �?
�? └──────────────────────────────────────────────────────────────────────�? �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?                        Hardware Abstraction Layer                          �?
�?                             (hal/radio.c)                                  �?
└─────────────────────────────────────────────────────────────────────────────�?
```

---

## 3. HCI命令发送到无线电中断的完整流程

### 3.1 HCI Create Connection 命令流程

```
┌─────────────────────────────────────────────────────────────────────────────�?
�?                   HCI_Create_Connection Command Flow                        �?
└─────────────────────────────────────────────────────────────────────────────�?

Host Application
      �?
      �?HCI_Create_Connection(BD_ADDR, packet_type, page_scan_rep_mode,
      �?                      clock_offset, allow_role_switch)
      �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[1] HCI Layer: hci_bredr_create_connection()                                �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?验证参数有效�?                                                          �?
�? �?检查是否已存在到该BD_ADDR的连�?                                         �?
�? �?发�?Command Status Event (Pending)                                      �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[2] ULL Layer: ull_bredr_page_start()                                       �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?分配 ull_bredr_page 上下�?                                              �?
�? �?初始�?page 参数:                                                        �?
�?   - bd_addr = target BD_ADDR                                               �?
�?   - page_scan_rep_mode = R0/R1/R2                                          �?
�?   - clock_offset = hint from inquiry                                       �?
�?   - n_page = Npage (based on rep_mode)                                     �?
�? �?计算 DAC (Device Access Code) from target LAP                            �?
�? �?设置 page state = PAGE_STATE_TRAIN_A                                     �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[3] Ticker Layer: ticker_bredr_page_start()                                 �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?计算 ticker 参数:                                                        �?
�?   - ticks_anchor = ticker_ticks_now_get()                                  �?
�?   - ticks_interval = PAGE_INTERVAL_US (1.28s for R1)                       �?
�?   - ticks_slot = ticker_bredr_scan_evt_dur_get()                           �?
�? �?调用 ticker_start():                                                     �?
�?   ticker_start(TICKER_INSTANCE_ID_CTLR,                                    �?
�?                TICKER_USER_ID_THREAD,                                      �?
�?                TICKER_ID_BREDR_PAGE,                                       �?
�?                ticks_anchor, ticks_interval, ...,                          �?
�?                ticker_bredr_page_cb,  /* expire callback */                �?
�?                page_context,                                               �?
�?                ticker_op_cb, NULL);                                        �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?[Ticker expires at scheduled time]
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[4] Ticker Callback: ticker_bredr_page_cb() [Mayfly Context]                �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?构建 lll_prepare_param:                                                  �?
�?   - ticks_at_expire = current ticker time                                  �?
�?   - remainder = sub-tick remainder                                         �?
�?   - lazy = missed intervals count                                          �?
�?   - force = forced execution flag                                          �?
�?   - param = page_context                                                   �?
�? �?调用 lll_bredr_page_prepare(&prepare_param)                              �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[5] LLL Prepare: lll_bredr_page_prepare() [High Priority ISR]               �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?检查是否可以执�?(abort check)                                           �?
�? �?计算当前 page 跳频信道:                                                  �?
�?   channel = lll_bredr_hop_channel_calc(clock, NULL, 79, uap_lap)           �?
�? �?配置无线�?                                                              �?
�?   - radio_freq_set(2402 + channel)                                         �?
�?   - radio_access_code_set(dac)                                             �?
�?   - radio_tx_power_set(page_tx_power)                                      �?
�?   - radio_pkt_configure(ID_PACKET)                                         �?
�? �?设置 ISR 回调:                                                           �?
�?   radio_isr_set(lll_bredr_page_isr, page_context)                          �?
�? �?启动无线电发�?                                                          �?
�?   radio_tx_enable()                                                        �?
�?   radio_tmr_start_us(ticks_at_expire + offset)                             �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?[Radio TX Complete / RX Window]
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[6] Radio ISR: lll_bredr_page_isr() [Radio Interrupt Context]               �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?检查中断原�?                                                            �?
�?   if (radio_is_tx_done()) {                                                �?
�?       /* ID packet sent, switch to RX for page response */                 �?
�?       radio_rx_enable();                                                   �?
�?       radio_tmr_rx_start(rx_window_start);                                 �?
�?   }                                                                        �?
�?   if (radio_is_rx_done()) {                                                �?
�?       if (radio_crc_is_valid()) {                                          �?
�?           /* Page response received! */                                    �?
�?           page_response_received = true;                                   �?
�?           /* Prepare FHS packet */                                         �?
�?           prepare_fhs_packet();                                            �?
�?       }                                                                    �?
�?   }                                                                        �?
�? �?更新跳频索引:                                                            �?
�?   page->hop_index = (page->hop_index + 1) % 32;                            �?
�? �?调度 Done 处理:                                                          �?
�?   ull_bredr_done(done_param);                                              �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[7] ULL Done: ull_bredr_page_done() [ULL_LOW Mayfly]                        �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?处理 page 结果:                                                          �?
�?   if (page_response_received) {                                            �?
�?       /* 停止 page ticker */                                               �?
�?       ticker_bredr_page_stop();                                            �?
�?       /* 分配连接上下�?*/                                                 �?
�?       conn = ull_bredr_conn_acquire();                                     �?
�?       /* 初始化连�?*/                                                     �?
�?       ull_bredr_conn_setup(conn, ROLE_CENTRAL, bd_addr, lt_addr);          �?
�?       /* 启动连接 ticker */                                                �?
�?       ticker_bredr_conn_start(conn_idx, conn, ...);                        �?
�?       /* 开�?LMP 连接建立序列 */                                          �?
�?       lmp_proc_connection_setup(conn);                                     �?
�?   } else if (page_timeout) {                                               �?
�?       /* Page 超时 */                                                      �?
�?       hci_bredr_evt_connection_complete(                                   �?
�?           BT_HCI_ERR_PAGE_TIMEOUT, 0, bd_addr, ...);                       �?
�?   }                                                                        �?
└─────────────────────────────────────────────────────────────────────────────�?
```

### 3.2 HCI ACL Data TX 流程

```
┌─────────────────────────────────────────────────────────────────────────────�?
�?                        HCI ACL Data TX Flow                                 �?
└─────────────────────────────────────────────────────────────────────────────�?

Host Stack (L2CAP)
      �?
      �?HCI ACL Data Packet (handle, PB_flag, BC_flag, data_len, data)
      �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[1] HCI Layer: hci_bredr_acl_data_tx()                                      �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?解析 ACL 数据包头:                                                       �?
�?   handle = (hdr[0] | (hdr[1] << 8)) & 0x0FFF                               �?
�?   pb_flag = (hdr[1] >> 4) & 0x03                                           �?
�?   bc_flag = (hdr[1] >> 6) & 0x03                                           �?
�?   data_len = hdr[2] | (hdr[3] << 8)                                        �?
�? �?查找连接上下�?                                                          �?
�?   conn = ull_bredr_conn_get(handle)                                        �?
�? �?验证连接状�?                                                            �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[2] ULL Layer: ull_bredr_tx_enqueue()                                       �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?分配 TX PDU 节点:                                                        �?
�?   tx_node = mem_acquire(&mem_tx_pool)                                      �?
�? �?构建 baseband PDU:                                                       �?
�?   - 设置 LLID (L2CAP start/continuation)                                   �?
�?   - 设置 flow bit                                                          �?
�?   - 复制 payload                                                           �?
�? �?选择包类�?(DM1/DH1/DM3/DH3/DM5/DH5):                                    �?
�?   pkt_type = select_packet_type(data_len, conn->link.acl_packet_type)      �?
�? �?加入 TX 队列:                                                            �?
�?   memq_enqueue(tx_node, &conn->lll.memq_tx.tail)                           �?
�? �?更新 TX 计数                                                             �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?[Connection Ticker Expires]
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[3] LLL Prepare: lll_bredr_conn_prepare()                                   �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?检�?TX 队列:                                                            �?
�?   tx_node = memq_peek(conn->lll.memq_tx.head)                              �?
�? �?如果有数据要发�?                                                        �?
�?   - 计算跳频信道                                                           �?
�?   - 配置无线�?TX                                                          �?
�?   - 设置 TX PDU                                                            �?
�? �?如果没有数据:                                                            �?
�?   - 发�?POLL �?(Central) 或等�?(Peripheral)                             �?
�? �?配置 RX 窗口接收 ACK                                                     �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?[Radio TX/RX Complete]
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[4] Radio ISR: lll_bredr_conn_isr()                                         �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?TX 完成后切换到 RX:                                                      �?
�?   radio_switch_complete_and_rx()                                           �?
�? �?RX 完成后检�?                                                           �?
�?   if (radio_crc_is_valid()) {                                              �?
�?       /* 解析接收的包�?*/                                                 �?
�?       arqn = (header >> 8) & 0x01;  /* ACK bit */                          �?
�?       seqn = (header >> 9) & 0x01;  /* Sequence */                         �?
�?       flow = (header >> 7) & 0x01;  /* Flow control */                     �?
�?                                                                            �?
�?       if (arqn == conn->lll.tx_seqn) {                                     �?
�?           /* TX 被确�? 可以发送下一个包 */                                �?
�?           tx_acked = true;                                                 �?
�?           conn->lll.tx_seqn ^= 1;  /* Toggle SEQN */                       �?
�?       }                                                                    �?
�?                                                                            �?
�?       if (seqn != conn->lll.rx_seqn) {                                     �?
�?           /* 新数据包, 需要处�?*/                                         �?
�?           rx_new_data = true;                                              �?
�?           conn->lll.rx_seqn ^= 1;                                          �?
�?       }                                                                    �?
�?   }                                                                        �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[5] ULL Done: ull_bredr_conn_done()                                         �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?处理 TX ACK:                                                             �?
�?   if (tx_acked) {                                                          �?
�?       /* 从队列移除已确认的包 */                                           �?
�?       tx_node = memq_dequeue(&conn->lll.memq_tx.head);                     �?
�?       mem_release(tx_node, &mem_tx_pool);                                  �?
�?       /* 发�?Number of Completed Packets 事件 */                          �?
�?       hci_num_completed_packets(handle, 1);                                �?
�?   }                                                                        �?
�? �?处理 RX 数据:                                                            �?
�?   if (rx_new_data) {                                                       �?
�?       /* 将数据上报给 Host */                                              �?
�?       ull_rx_put(link, rx_node);                                           �?
�?       ull_rx_sched();                                                      �?
�?   }                                                                        �?
└─────────────────────────────────────────────────────────────────────────────�?
```

---

## 4. 无线电中断数据接收后的完整流转流�?

### 4.1 ACL 数据接收流程

```
┌─────────────────────────────────────────────────────────────────────────────�?
�?                     ACL Data RX Flow (Interrupt to Host)                    �?
└─────────────────────────────────────────────────────────────────────────────�?

                              Radio Hardware
                                    �?
                                    �?[RX Complete Interrupt]
                                    �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[1] Radio ISR: lll_bredr_conn_isr() [Highest Priority - Radio IRQ]          �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?读取接收状�?                                                            �?
�?   crc_ok = radio_crc_is_valid();                                           �?
�?   rssi = radio_rssi_get();                                                 �?
�?                                                                            �?
�? �?如果 CRC 正确:                                                           �?
�?   /* 分配 RX 节点 */                                                       �?
�?   rx_node = ull_pdu_rx_alloc();                                            �?
�?   if (rx_node == NULL) {                                                   �?
�?       /* 无可用缓冲区, 丢弃 */                                             �?
�?       goto isr_done;                                                       �?
�?   }                                                                        �?
�?                                                                            �?
�?   /* 从无线电缓冲区复制数�?*/                                             �?
�?   radio_pkt_rx(rx_node->pdu);                                              �?
�?                                                                            �?
�?   /* 解析 baseband 包头 */                                                 �?
�?   lt_addr = pdu[0] & 0x07;                                                 �?
�?   type = (pdu[0] >> 3) & 0x0F;                                             �?
�?   flow = (pdu[0] >> 7) & 0x01;                                             �?
�?   arqn = (pdu[1] >> 0) & 0x01;                                             �?
�?   seqn = (pdu[1] >> 1) & 0x01;                                             �?
�?                                                                            �?
�?   /* 检查是否是新数�?(SEQN 变化) */                                       �?
�?   if (seqn != conn->lll.rx_seqn) {                                         �?
�?       conn->lll.rx_seqn ^= 1;                                              �?
�?       rx_node->hdr.type = NODE_RX_TYPE_DC_PDU;                             �?
�?       rx_node->hdr.handle = conn->handle;                                  �?
�?                                                                            �?
�?       /* 填充 footer 信息 */                                               �?
�?       rx_node->rx_ftr.rssi = rssi;                                         �?
�?       rx_node->rx_ftr.ticks_anchor = ticks_at_expire;                      �?
�?                                                                            �?
�?       /* 标记需要上�?*/                                                   �?
�?       rx_enqueue = true;                                                   �?
�?   }                                                                        �?
�?                                                                            �?
�? �?更新 ARQN (发�?ACK):                                                    �?
�?   conn->lll.tx_arqn = 1;  /* Will ACK in next TX */                        �?
�?                                                                            �?
�? �?调度 Done 处理 (通过 Mayfly):                                            �?
�?   done_param.param = conn;                                                 �?
�?   done_param.extra.trx_cnt = 1;                                            �?
�?   done_param.extra.crc_valid = crc_ok;                                     �?
�?   mayfly_enqueue(TICKER_USER_ID_LLL, TICKER_USER_ID_ULL_HIGH,              �?
�?                  0, &mfy_done);                                            �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?[Mayfly Scheduled]
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[2] ULL HIGH: ull_bredr_rx_demux() [ULL_HIGH Mayfly Priority]               �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?�?LLL RX 队列获取节点:                                                  �?
�?   while ((link = memq_dequeue(&memq_ll_rx.head))) {                        �?
�?       rx_node = (struct node_rx_pdu *)link->mem;                           �?
�?                                                                            �?
�?       /* 根据类型分发 */                                                   �?
�?       switch (rx_node->hdr.type) {                                         �?
�?       case NODE_RX_TYPE_DC_PDU:                                            �?
�?           /* ACL 数据�?LMP PDU */                                         �?
�?           ull_bredr_rx(link, &rx_node);                                    �?
�?           break;                                                           �?
�?       case NODE_RX_TYPE_EVENT_DONE:                                        �?
�?           /* 事件完成处理 */                                               �?
�?           ull_bredr_done(rx_node->param);                                  �?
�?           break;                                                           �?
�?       }                                                                    �?
�?   }                                                                        �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[3] ULL RX Handler: ull_bredr_rx() [ULL_HIGH Context]                       �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?获取连接上下�?                                                          �?
�?   conn = ull_bredr_conn_get(rx_node->hdr.handle);                          �?
�?                                                                            �?
�? �?解析 payload header:                                                     �?
�?   llid = pdu[0] & 0x03;                                                    �?
�?   flow = (pdu[0] >> 2) & 0x01;                                             �?
�?   length = ((pdu[0] >> 3) & 0x1F) | ((pdu[1] & 0x1F) << 5);                �?
�?                                                                            �?
�? �?根据 LLID 分发:                                                          �?
�?   switch (llid) {                                                          �?
�?   case BREDR_LLID_LMP:  /* LMP PDU */                                      �?
�?       /* 交给 LMP 处理 */                                                  �?
�?       ull_bredr_lmp_rx(conn, &pdu[2], length);                             �?
�?       /* 释放 RX 节点 (LMP 不上�?Host) */                                 �?
�?       mem_release(rx_node, &mem_rx_pool);                                  �?
�?       break;                                                               �?
�?                                                                            �?
�?   case BREDR_LLID_START:       /* L2CAP Start */                           �?
�?   case BREDR_LLID_CONTINUATION: /* L2CAP Continuation */                   �?
�?       /* ACL 数据, 上报�?Host */                                          �?
�?       rx_node->hdr.type = NODE_RX_TYPE_DC_PDU;                             �?
�?       ull_rx_put(link, rx_node);                                           �?
�?       ull_rx_sched();                                                      �?
�?       break;                                                               �?
�?   }                                                                        �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?[If ACL Data]
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[4] RX Scheduler: ull_rx_sched() �?ll_rx_sched()                            �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?触发 RX 信号�?工作队列:                                                 �?
�?   k_sem_give(&sem_rx);  /* �?k_work_submit(&rx_work) */                   �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?[Thread Context Switch]
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[5] RX Thread: recv_thread() [Thread Priority]                              �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?等待 RX 信号:                                                            �?
�?   k_sem_take(&sem_rx, K_FOREVER);                                          �?
�?                                                                            �?
�? �?�?ULL RX 队列获取:                                                      �?
�?   while ((rx_node = ll_rx_get())) {                                        �?
�?       switch (rx_node->hdr.type) {                                         �?
�?       case NODE_RX_TYPE_DC_PDU:                                            �?
�?           /* 构建 HCI ACL 数据�?*/                                        �?
�?           hci_acl_data_rx(rx_node);                                        �?
�?           break;                                                           �?
�?       }                                                                    �?
�?       ll_rx_release(rx_node);                                              �?
�?   }                                                                        �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[6] HCI Layer: hci_acl_data_rx()                                            �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?构建 HCI ACL 数据�?                                                     �?
�?   hci_hdr[0] = handle & 0xFF;                                              �?
�?   hci_hdr[1] = ((handle >> 8) & 0x0F) | (pb_flag << 4) | (bc_flag << 6);   �?
�?   hci_hdr[2] = data_len & 0xFF;                                            �?
�?   hci_hdr[3] = (data_len >> 8) & 0xFF;                                     �?
�?                                                                            �?
�? �?通过 HCI Transport 发送给 Host:                                          �?
�?   bt_send(buf);  /* �?hci_driver->send() */                               �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?
                           Host Stack (L2CAP)
```

### 4.2 LMP PDU 接收处理流程

```
┌─────────────────────────────────────────────────────────────────────────────�?
�?                        LMP PDU RX Processing Flow                           �?
└─────────────────────────────────────────────────────────────────────────────�?

[From ull_bredr_rx() when LLID == BREDR_LLID_LMP]
      �?
      �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[1] LMP Demux: ull_bredr_lmp_rx()                                           �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?解析 LMP PDU �?                                                         �?
�?   tid = pdu[0] & 0x01;           /* Transaction ID */                      �?
�?   opcode = (pdu[0] >> 1) & 0x7F; /* Opcode */                              �?
�?                                                                            �?
�? �?检查是否是扩展 opcode:                                                   �?
�?   if (opcode >= LMP_ESCAPE_1 && opcode <= LMP_ESCAPE_4) {                  �?
�?       ext_opcode = pdu[1];                                                 �?
�?       is_extended = true;                                                  �?
�?   }                                                                        �?
�?                                                                            �?
�? �?分发到对应的 LMP 过程处理�?                                             �?
�?   lmp_proc_rx_dispatch(conn, opcode, ext_opcode, tid, &pdu[1], len-1);     �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[2] LMP Procedure Dispatch: lmp_proc_rx_dispatch()                          �?
├─────────────────────────────────────────────────────────────────────────────�?
�? switch (opcode) {                                                          �?
�? /*=== Connection Setup ===*/                                               �?
�? case LMP_HOST_CONNECTION_REQ:                                              �?
�?     lmp_proc_host_conn_req_rx(conn, tid);                                  �?
�?     break;                                                                 �?
�? case LMP_SETUP_COMPLETE:                                                   �?
�?     lmp_proc_setup_complete_rx(conn, tid);                                 �?
�?     break;                                                                 �?
�?                                                                            �?
�? /*=== Version/Features ===*/                                               �?
�? case LMP_VERSION_REQ:                                                      �?
�?     lmp_proc_version_rx(conn, pdu, len);                                   �?
�?     break;                                                                 �?
�? case LMP_VERSION_RES:                                                      �?
�?     lmp_proc_version_rx(conn, pdu, len);                                   �?
�?     break;                                                                 �?
�? case LMP_FEATURES_REQ:                                                     �?
�? case LMP_FEATURES_RES:                                                     �?
�?     lmp_proc_features_rx(conn, pdu, len);                                  �?
�?     break;                                                                 �?
�?                                                                            �?
�? /*=== Authentication ===*/                                                 �?
�? case LMP_AU_RAND:                                                          �?
�?     lmp_proc_au_rand_rx(conn, pdu, tid);                                   �?
�?     break;                                                                 �?
�? case LMP_SRES:                                                             �?
�?     lmp_proc_sres_rx(conn, pdu, tid);                                      �?
�?     break;                                                                 �?
�? case LMP_IN_RAND:                                                          �?
�?     lmp_proc_in_rand_rx(conn, pdu, tid);                                   �?
�?     break;                                                                 �?
�? case LMP_COMB_KEY:                                                         �?
�?     lmp_proc_comb_key_rx(conn, pdu, tid);                                  �?
�?     break;                                                                 �?
�?                                                                            �?
�? /*=== Encryption ===*/                                                     �?
�? case LMP_ENCRYPTION_MODE_REQ:                                              �?
�?     lmp_proc_enc_mode_req_rx(conn, pdu, tid);                              �?
�?     break;                                                                 �?
�? case LMP_ENCRYPTION_KEY_SIZE_REQ:                                          �?
�?     lmp_proc_enc_key_size_req_rx(conn, pdu, tid);                          �?
�?     break;                                                                 �?
�? case LMP_START_ENCRYPTION_REQ:                                             �?
�?     lmp_proc_start_enc_req_rx(conn, pdu, tid);                             �?
�?     break;                                                                 �?
�? case LMP_STOP_ENCRYPTION_REQ:                                              �?
�?     lmp_proc_stop_enc_req_rx(conn, pdu, tid);                              �?
�?     break;                                                                 �?
�?                                                                            �?
�? /*=== Response PDUs ===*/                                                  �?
�? case LMP_ACCEPTED:                                                         �?
�?     lmp_proc_accepted_rx(conn, pdu, tid);                                  �?
�?     break;                                                                 �?
�? case LMP_NOT_ACCEPTED:                                                     �?
�?     lmp_proc_not_accepted_rx(conn, pdu, tid);                              �?
�?     break;                                                                 �?
�?                                                                            �?
�? /*=== Extended Opcodes ===*/                                               �?
�? case LMP_ESCAPE_4:                                                         �?
�?     lmp_proc_extended_rx(conn, ext_opcode, &pdu[1], len-1, tid);           �?
�?     break;                                                                 �?
�?                                                                            �?
�? /*=== Detach ===*/                                                         �?
�? case LMP_DETACH:                                                           �?
�?     lmp_proc_detach_rx(conn, pdu, tid);                                    �?
�?     break;                                                                 �?
�?                                                                            �?
�? default:                                                                   �?
�?     /* Unknown opcode, send LMP_not_accepted */                            �?
�?     ull_bredr_send_pdu_not_acc(conn_idx, opcode,                           �?
�?                                BT_HCI_ERR_UNKNOWN_LMP_PDU, tid);           �?
�?     break;                                                                 �?
�? }                                                                          �?
└─────────────────────────────────────────────────────────────────────────────�?
```

---

## 5. Link Controller 完整状态机

### 5.1 主状态机 (lc_state)

```
┌─────────────────────────────────────────────────────────────────────────────�?
�?                   Link Controller State Machine                             �?
�?                        (enum lc_state)                                      �?
└─────────────────────────────────────────────────────────────────────────────�?

                              ┌──────────────�?
                              �?  LC_FREE    �?
                              �? (Initial)   �?
                              └──────┬───────�?
                                     �?Page Response / Page Scan Response
                                     �?
                              ┌──────────────�?
                              �?LC_CONNECTED │◄────────────────────────────────�?
                              �?             �?                                 �?
                              └──────┬───────�?                                 �?
                                     �?                                         �?
         ┌───────────────────────────┼───────────────────────────�?            �?
         �?                          �?                          �?            �?
         �?                          �?                          �?            �?
┌─────────────────�?     ┌─────────────────�?     ┌─────────────────�?        �?
�?VERSION/FEATURE �?     �?AUTHENTICATION  �?     �?  ENCRYPTION    �?        �?
�?   EXCHANGE     �?     �?   SEQUENCE     �?     �?   SEQUENCE     �?        �?
└────────┬────────�?     └────────┬────────�?     └────────┬────────�?        �?
         �?                       �?                       �?                  �?
         �?                       �?                       �?                  �?
         �?              ┌─────────────────�?              �?                  �?
         �?              │LC_WAIT_AU_RAND_ �?              �?                  �?
         �?              �?   RSP/INIT     �?              �?                  �?
         �?              └────────┬────────�?              �?                  �?
         �?                       �?                       �?                  �?
         �?                       �?                       �?                  �?
         �?              ┌─────────────────�?              �?                  �?
         �?              �?LC_WAIT_SRES_   �?              �?                  �?
         �?              �?   RSP/INIT     �?              �?                  �?
         �?              └────────┬────────�?              �?                  �?
         �?                       �?                       �?                  �?
         �?                       �?                       �?                  �?
         �?              ┌─────────────────�?     ┌─────────────────�?        �?
         �?              �?LC_WAIT_KEY_    �?     │LC_WAIT_ENC_MODE �?        �?
         �?              �?   EXCH         �?     �?    _CFM        �?        �?
         �?              └────────┬────────�?     └────────┬────────�?        �?
         �?                       �?                       �?                  �?
         �?                       �?                       �?                  �?
         �?                       �?              ┌─────────────────�?        �?
         �?                       �?              │LC_WAIT_ENC_SIZE �?        �?
         �?                       �?              �?    _CFM        �?        �?
         �?                       �?              └────────┬────────�?        �?
         �?                       �?                       �?                  �?
         �?                       �?                       �?                  �?
         �?                       �?              ┌─────────────────�?        �?
         �?                       �?              │LC_WAIT_ENC_START�?        �?
         �?                       �?              �?    _CFM        �?        �?
         �?                       �?              └────────┬────────�?        �?
         �?                       �?                       �?                  �?
         └────────────────────────┴────────────────────────�?                  �?
                                     �?                                         �?
                                     �?All procedures complete                  �?
                                     �?                                         �?
                              ┌──────────────�?                                 �?
                              �?LC_CONNECTED │──────────────────────────────────�?
                              �? (Stable)    �?
                              └──────┬───────�?
                                     �?
         ┌───────────────────────────┼───────────────────────────�?
         �?                          �?                          �?
         �?                          �?                          �?
┌─────────────────�?     ┌─────────────────�?     ┌─────────────────�?
�? POWER MODES    �?     �? ROLE SWITCH    �?     �?  SSP/PAIRING   �?
�?(Sniff/Hold/    �?     �?                �?     �?                �?
�? Park)          �?     �?                �?     �?                �?
└────────┬────────�?     └────────┬────────�?     └────────┬────────�?
         �?                       �?                       �?
         �?                       �?                       �?
┌─────────────────�?     ┌─────────────────�?     ┌─────────────────�?
│LC_WAIT_SNIFF_   �?     │LC_WAIT_SWITCH_  �?     │LC_WAIT_HL_IO_   �?
�?   REQ          �?     �?   CFM          �?     �?  CAP_INIT      �?
└────────┬────────�?     └────────┬────────�?     └────────┬────────�?
         �?                       �?                       �?
         �?                       �?                       �?
┌─────────────────�?     ┌─────────────────�?     ┌─────────────────�?
│LC_WAIT_SNIFF_   �?     │LC_WAIT_SWITCH_  �?     │LC_PUB_KEY_      �?
�? ACC_TX_CFM     �?     �?   CMP          �?     �?HEADER_INIT_LOC �?
└────────┬────────�?     └────────┬────────�?     └────────┬────────�?
         �?                       �?                       �?
         �?                       �?                       �?
         �?                       �?              ┌─────────────────�?
         �?                       �?              │LC_WAIT_DHKEY_   �?
         �?                       �?              �?   CHECK        �?
         �?                       �?              └────────┬────────�?
         �?                       �?                       �?
         └────────────────────────┴────────────────────────�?
                                     �?
                                     �?Return to LC_CONNECTED
                                     �?
                              ┌──────────────�?
                              �?LC_CONNECTED �?
                              └──────┬───────�?
                                     �?
                                     �?LMP_detach received/sent
                                     �?
                              ┌──────────────�?
                              │LC_WAIT_DETACH�?
                              �?  _REQ_TX_CFM�?
                              └──────┬───────�?
                                     �?
                                     �?
                              ┌──────────────�?
                              │LC_WAIT_DISC_ �?
                              �?    TO       �?
                              └──────┬───────�?
                                     �?
                                     �?
                              ┌──────────────�?
                              �?  LC_FREE    �?
                              └──────────────�?
```

### 5.2 SSP (Secure Simple Pairing) 详细状态机

```
┌─────────────────────────────────────────────────────────────────────────────�?
�?                   SSP State Machine (Initiator)                             �?
└─────────────────────────────────────────────────────────────────────────────�?

┌──────────────────�?
�?LC_CONNECTED     �?
�?(SSP Triggered)  �?
└────────┬─────────�?
         �?Host requests authentication
         �?(HCI_Authentication_Requested)
         �?
┌──────────────────�?    Send LMP_IO_capability_req
│LC_WAIT_HL_IO_CAP │────────────────────────────────────────�?
�?   _INIT         �?                                        �?
└────────┬─────────�?                                        �?
         �?Host provides IO capabilities                     �?
         �?(HCI_IO_Capability_Request_Reply)                 �?
         �?                                                  �?
┌──────────────────�?                                        �?
│LC_WAIT_IO_CAP_   │◄────────────────────────────────────────�?
�? INIT_CFM        �?    Wait for LMP_IO_capability_res
└────────┬─────────�?
         �?Received LMP_IO_capability_res
         �?Determine association model
         �?
┌──────────────────�?
│LC_PUB_KEY_HEADER �?    Send LMP_encapsulated_header (P-192/P-256)
�?  _INIT_LOC      �?
└────────┬─────────�?
         �?
         �?
┌──────────────────�?
│LC_PUB_KEY_PAYLOAD�?    Send LMP_encapsulated_payload (Public Key X,Y)
�?  _INIT_LOC      �?    (Multiple PDUs for full key)
└────────┬─────────�?
         �?
         �?
┌──────────────────�?
│LC_WAIT_PUB_KEY_  �?    Wait for peer's LMP_encapsulated_header
�?HEADER_INIT_PEER �?
└────────┬─────────�?
         �?
         �?
┌──────────────────�?
│LC_WAIT_PUB_KEY_  �?    Wait for peer's LMP_encapsulated_payload
│PAYLOAD_INIT_PEER �?
└────────┬─────────�?
         �?Compute DHKey in background
         �?
         ├─────────────────────────────────────────────────────�?
         �?Numeric Comparison                                  �?Passkey Entry
         �?                                                    �?
┌──────────────────�?                              ┌──────────────────�?
│LC_WAIT_NUM_COMP_ �?                              │LC_WAIT_PASSKEY_  �?
│COMM_INIT_CONF    �?                              �?  HL_RPLY        �?
└────────┬─────────�?                              └────────┬─────────�?
         �?Send LMP_simple_pairing_confirm                  �?Host provides passkey
         �?                                                 �?
┌──────────────────�?                              ┌──────────────────�?
│LC_WAIT_NUM_COMP_ �?                              │LC_WAIT_PASSKEY_  �?
│COMM_INIT_RANDN_  �?                              �? COMM_RSP_PEER   �?
�?    PEER         �?                              └────────┬─────────�?
└────────┬─────────�?                                       �?
         �?Received peer's confirm                          �?
         �?Send LMP_simple_pairing_number                   �?
         �?                                                 �?
┌──────────────────�?                                       �?
│LC_WAIT_NUM_COMP_ �?                                       �?
�? USER_CONF_INIT  �?                                       �?
└────────┬─────────�?                                       �?
         �?User confirms (HCI_User_Confirmation_Request_Reply)
         �?                                                 �?
         └──────────────────────┬───────────────────────────�?
                                �?
                                �?
                       ┌──────────────────�?
                       │LC_WAIT_DHKEY_    �?    Wait for DHKey computation
                       �?  COMPUTING      �?
                       └────────┬─────────�?
                                �?
                                �?
                       ┌──────────────────�?
                       │LC_WAIT_DHKEY_    �?    Send LMP_DHkey_check
                       │CHECK_INIT_PEER   �?
                       └────────┬─────────�?
                                �?Received peer's DHkey_check
                                �?Verify check value
                                �?
                       ┌──────────────────�?
                       │LC_WAIT_DHKEY_    �?    Send LMP_DHkey_check
                       �? CHECK_INIT_CFM  �?
                       └────────┬─────────�?
                                �?
                                �?Link key generated!
                                �?HCI_Simple_Pairing_Complete
                                �?HCI_Link_Key_Notification
                                �?
                       ┌──────────────────�?
                       �?  LC_CONNECTED   �?
                       �?(SSP Complete)   �?
                       └──────────────────�?
```

### 5.3 加密状态机

```
┌─────────────────────────────────────────────────────────────────────────────�?
�?                   Encryption State Machine                                  �?
└─────────────────────────────────────────────────────────────────────────────�?

                       ┌──────────────────�?
                       �?  LC_CONNECTED   �?
                       �?(Unencrypted)    �?
                       └────────┬─────────�?
                                �?HCI_Set_Connection_Encryption(enable=1)
                                �?or Authentication complete with encryption required
                                �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?                        ENCRYPTION START SEQUENCE                            �?
├─────────────────────────────────────────────────────────────────────────────�?
�?                                                                             �?
�? Central                                              Peripheral             �?
�?    �?                                                     �?                �?
�?    │──────── LMP_encryption_mode_req(mode=1) ────────────►│                 �?
�?    �?                                                     �?                �?
�?    │◄─────────────── LMP_accepted ────────────────────────�?                �?
�?    �?                                                     �?                �?
�?    │──────── LMP_encryption_key_size_req(size=16) ───────►│                 �?
�?    �?                                                     �?                �?
�?    │◄─────────────── LMP_accepted ────────────────────────�?                �?
�?    �?        (or LMP_encryption_key_size_req with         �?                �?
�?    �?         negotiated size)                            �?                �?
�?    �?                                                     �?                �?
�?    │──────── LMP_start_encryption_req(EN_RAND) ──────────►│                 �?
�?    �?                                                     �?                �?
�?    │◄─────────────── LMP_accepted ────────────────────────�?                �?
�?    �?                                                     �?                �?
�?    �?        [Both sides compute encryption key]          �?                �?
�?    �?        Kc = E3(Link_Key, EN_RAND, ACO)             �?                �?
�?    �?        [Both sides enable E0 encryption]            �?                �?
�?    �?                                                     �?                �?
└─────────────────────────────────────────────────────────────────────────────�?
                                �?
                                �?
                       ┌──────────────────�?
                       �?  LC_CONNECTED   �?
                       �? (Encrypted)     �?
                       └────────┬─────────�?
                                �?
         ┌──────────────────────┼──────────────────────�?
         �?                     �?                     �?
         �?                     �?                     �?
┌─────────────────�? ┌─────────────────�? ┌─────────────────�?
�?ENCRYPTION STOP �? �?ENCRYPTION      �? �?ENCRYPTION      �?
�?                �? �?PAUSE/RESUME    �? �?KEY REFRESH     �?
�?                �? �?(for Role Switch�? �?                �?
�?                �? �? or Key Refresh)�? �?                �?
└────────┬────────�? └────────┬────────�? └────────┬────────�?
         �?                   �?                   �?
         �?                   �?                   �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?                        ENCRYPTION PAUSE SEQUENCE                            �?
├─────────────────────────────────────────────────────────────────────────────�?
�?                                                                             �?
�? Initiator                                            Responder              �?
�?    �?                                                     �?                �?
�?    │──────── LMP_pause_encryption_req ───────────────────►│                 �?
�?    �?                                                     �?                �?
�?    │◄─────────────── LMP_accepted ────────────────────────�?                �?
�?    �?                                                     �?                �?
�?    �?        [Both sides disable encryption]              �?                �?
�?    �?                                                     �?                �?
�?    �?        ... (Role switch or key refresh) ...         �?                �?
�?    �?                                                     �?                �?
�?    │──────── LMP_resume_encryption_req ──────────────────►│                 �?
�?    �?                                                     �?                �?
�?    │◄─────────────── LMP_accepted ────────────────────────�?                �?
�?    �?                                                     �?                �?
�?    �?        [Both sides re-enable encryption]            �?                �?
�?    �?                                                     �?                �?
└─────────────────────────────────────────────────────────────────────────────�?

State Transitions:
┌──────────────────────────────────────────────────────────────────────────────�?
�?LC_CONNECTED ──�?LC_WAIT_ENC_MODE_CFM ──�?LC_WAIT_ENC_SIZE_CFM               �?
�?                                                   �?                         �?
�?                                                   �?                         �?
�?LC_CONNECTED ◄── LC_WAIT_ENC_START_CFM ◄── LC_WAIT_ENC_SLV_SIZE              �?
�?(Encrypted)                                                                   �?
�?                                                                              �?
�?For EPR (Encryption Pause Resume):                                           �?
�?LC_CONNECTED ──�?LC_WAIT_EPR_ENC_PAUSE_REQ_MST_INIT                          �?
�?            ──�?LC_WAIT_EPR_RSP                                              �?
�?            ──�?LC_WAIT_EPR_ENC_RESUME_REQ_MST_RSP                           �?
�?            ──�?LC_CONNECTED (Re-encrypted)                                  �?
└──────────────────────────────────────────────────────────────────────────────�?
```

---

## 6. LMP 事务冲突管理

### 6.1 LMP 事务冲突检测与处理

```
┌─────────────────────────────────────────────────────────────────────────────�?
�?                   LMP Transaction Collision Management                      �?
└─────────────────────────────────────────────────────────────────────────────�?

LMP 事务使用 Transaction ID (TID) 来区分发起方:
  - TID = 0: �?Central 发起
  - TID = 1: �?Peripheral 发起

冲突场景:
┌─────────────────────────────────────────────────────────────────────────────�?
�? Device A (Central)                        Device B (Peripheral)            �?
�?      �?                                          �?                         �?
�?      │──── LMP_encryption_mode_req (TID=0) ─────►│                          �?
�?      �?                                          �?                         �?
�?      │◄─── LMP_switch_req (TID=1) ───────────────�? �?冲突!                �?
�?      �?                                          �?                         �?
�? 两个过程同时进行, 需要冲突解�?                                             �?
└─────────────────────────────────────────────────────────────────────────────�?

冲突解决规则 (Core Spec Vol 2, Part C, Section 5.3):
┌─────────────────────────────────────────────────────────────────────────────�?
�?                        Collision Resolution Rules                           �?
├─────────────────────────────────────────────────────────────────────────────�?
�?                                                                             �?
�? 1. 相同过程冲突 (Same Procedure Collision):                                �?
�?    - Central 的请求优�?                                                   �?
�?    - Peripheral 应该接受 Central 的请求并放弃自己的请�?                   �?
�?                                                                             �?
�? 2. 不同过程冲突 (Different Procedure Collision):                           �?
�?    - 根据过程优先级决�?                                                   �?
�?    - 高优先级过程继续, 低优先级过程等待                                    �?
�?                                                                             �?
�? 过程优先�?(从高到低):                                                     �?
�?    1. Detach                                                                �?
�?    2. Authentication                                                        �?
�?    3. Encryption                                                            �?
�?    4. Role Switch                                                           �?
�?    5. Hold/Sniff/Park                                                       �?
�?    6. QoS                                                                   �?
�?    7. Others                                                                �?
�?                                                                             �?
�? 3. 使用 LMP_not_accepted 拒绝冲突请求:                                     �?
�?    - Error code: LMP_ERROR_TRANSACTION_COLLISION (0x23)                    �?
�?                                                                             �?
└─────────────────────────────────────────────────────────────────────────────�?
```

### 6.2 冲突管理实现

```c
/*
 * 冲突检测数据结�?(�?ull_bredr_conn �?
 */
struct ull_bredr_local_trans {
    uint8_t opcode;      /* 当前本地事务�?opcode */
    uint8_t opcode_ext;  /* 扩展 opcode (如果适用) */
    uint8_t in_use;      /* LC_UTIL_NOT_USED / LC_UTIL_INUSED */
};

struct ull_bredr_req {
    /* 本地请求标志 */
    bool loc_name_req;
    bool loc_remote_extended_req;
    bool loc_detach_req;
    bool loc_cpt_req;           /* Change Packet Type */
    bool loc_enc_req;
    bool loc_auth_req;
    bool loc_key_exchange_req;
    bool loc_enc_key_refresh;
    bool loc_switch_req;
    bool loc_vers_req;
    bool loc_flow_spec_req;

    /* 对端请求标志 */
    bool peer_switch_req;
    bool peer_auth_req;
    bool peer_enc_req;
    bool peer_detach_req;
    bool peer_enc_key_refresh;

    /* 内部请求 */
    bool restart_enc_req;
    bool master_key_req;
};

/*
 * 冲突检测函�?
 */
static bool lmp_proc_check_collision(struct ull_bredr_conn *conn,
                                     uint8_t opcode, uint8_t tid)
{
    /* 检查是否有本地事务正在进行 */
    if (conn->local_trans.in_use == LC_UTIL_INUSED) {
        /* 检查是否是相同过程 */
        if (conn->local_trans.opcode == opcode) {
            /* 相同过程冲突 */
            if (conn->link.role == BREDR_ROLE_CENTRAL) {
                /* Central 优先, 拒绝对端请求 */
                return true;  /* Collision detected */
            } else {
                /* Peripheral 放弃本地请求 */
                lmp_proc_abort_local_trans(conn);
                return false;
            }
        }
        
        /* 不同过程, 检查优先级 */
        uint8_t local_prio = lmp_proc_get_priority(conn->local_trans.opcode);
        uint8_t peer_prio = lmp_proc_get_priority(opcode);
        
        if (local_prio > peer_prio) {
            /* 本地过程优先级更�? 拒绝对端 */
            return true;
        } else if (local_prio < peer_prio) {
            /* 对端过程优先级更�? 暂停本地 */
            lmp_proc_pause_local_trans(conn);
            return false;
        } else {
            /* 相同优先�? Central 优先 */
            if (conn->link.role == BREDR_ROLE_CENTRAL) {
                return true;
            } else {
                lmp_proc_pause_local_trans(conn);
                return false;
            }
        }
    }
    
    return false;  /* No collision */
}

/*
 * 冲突处理示例
 */
static void lmp_proc_enc_mode_req_rx(struct ull_bredr_conn *conn,
                                     uint8_t *pdu, uint8_t tid)
{
    /* 检查冲�?*/
    if (lmp_proc_check_collision(conn, LMP_ENCRYPTION_MODE_REQ, tid)) {
        /* 发�?LMP_not_accepted */
        ull_bredr_send_pdu_not_acc(conn_idx, LMP_ENCRYPTION_MODE_REQ,
                                   BT_HCI_ERR_LMP_TRANSACTION_COLLISION,
                                   tid);
        return;
    }
    
    /* 记录对端请求 */
    conn->req.peer_enc_req = true;
    
    /* 处理加密模式请求 */
    uint8_t enc_mode = pdu[0];
    
    if (enc_mode == ENC_POINT_TO_POINT) {
        /* 接受加密请求 */
        conn->enc.new_enc_mode = enc_mode;
        ull_bredr_send_pdu_acc(conn_idx, LMP_ENCRYPTION_MODE_REQ, tid);
        
        /* 更新状�?*/
        conn->lc_state = BREDR_LC_WAIT_ENC_SIZE_CFM;
    } else {
        /* 不支持的加密模式 */
        ull_bredr_send_pdu_not_acc(conn_idx, LMP_ENCRYPTION_MODE_REQ,
                                   BT_HCI_ERR_UNSUPPORTED_FEATURE, tid);
    }
}
```

---

## 7. Ticker 调度�?BLE/BR/EDR 共存

### 7.1 Ticker 节点分配

```
┌─────────────────────────────────────────────────────────────────────────────�?
�?                        BR/EDR Ticker Node Allocation                        �?
└─────────────────────────────────────────────────────────────────────────────�?

Ticker ID 枚举 (定义�?lll.h):

┌─────────────────────────────────────────────────────────────────────────────�?
�? BLE Ticker IDs                                                              �?
�? ├── TICKER_ID_LLL_PREEMPT                                                  �?
�? ├── TICKER_ID_ADV_STOP                                                     �?
�? ├── TICKER_ID_ADV_BASE ... TICKER_ID_ADV_LAST                              �?
�? ├── TICKER_ID_SCAN_STOP                                                    �?
�? ├── TICKER_ID_SCAN_BASE ... TICKER_ID_SCAN_LAST                            �?
�? ├── TICKER_ID_CONN_BASE ... TICKER_ID_CONN_LAST                            �?
�? └── ...                                                                     �?
├─────────────────────────────────────────────────────────────────────────────�?
�? BR/EDR Ticker IDs (CONFIG_BT_CTLR_BREDR)                                   �?
�? ├── TICKER_ID_BREDR_INQUIRY          (1�?                                 �?
�? ├── TICKER_ID_BREDR_INQUIRY_SCAN     (1�?                                 �?
�? ├── TICKER_ID_BREDR_PAGE             (1�?                                 �?
�? ├── TICKER_ID_BREDR_PAGE_SCAN        (1�?                                 �?
�? ├── TICKER_ID_BREDR_CONN_BASE        (CONFIG_BT_CTLR_BREDR_MAX_CONN�?     �?
�? �?  └── TICKER_ID_BREDR_CONN_LAST                                          �?
�? ├── TICKER_ID_BREDR_SCO_BASE         (CONFIG_BT_CTLR_BREDR_SCO_MAX�?      �?
�? �?  └── TICKER_ID_BREDR_SCO_LAST                                           �?
�? └── TICKER_ID_BREDR_ESCO_BASE        (CONFIG_BT_CTLR_BREDR_ESCO_MAX�?     �?
�?     └── TICKER_ID_BREDR_ESCO_LAST                                          �?
├─────────────────────────────────────────────────────────────────────────────�?
�? TICKER_ID_MAX                                                               �?
└─────────────────────────────────────────────────────────────────────────────�?

默认配置 (Kconfig):
  CONFIG_BT_CTLR_BREDR_MAX_CONN = 4
  CONFIG_BT_CTLR_BREDR_SCO_MAX = 2
  CONFIG_BT_CTLR_BREDR_ESCO_MAX = 2

�?BR/EDR Ticker 节点�?
  BT_BREDR_TICKER_NODES = 4 + 4 + 2 + 2 = 12
```

### 7.2 BLE/BR/EDR 共存调度

```
┌─────────────────────────────────────────────────────────────────────────────�?
�?                   BLE/BR/EDR Coexistence Scheduling                         �?
└─────────────────────────────────────────────────────────────────────────────�?

时间线示�?(SCO + BLE Connection + BR/EDR ACL):

Time ──────────────────────────────────────────────────────────────────────────�?

     ┌─────�?    ┌─────�?    ┌─────�?    ┌─────�?    ┌─────�?
SCO  �?TX  �?    �?TX  �?    �?TX  �?    �?TX  �?    �?TX  �? (Tsco=6 slots)
     �?RX  �?    �?RX  �?    �?RX  �?    �?RX  �?    �?RX  �? MUST_EXPIRE
     └─────�?    └─────�?    └─────�?    └─────�?    └─────�?
     0     2     6     8    12    14    18    20    24    26  (slots)

           ┌───────────�?                ┌───────────�?
BLE        �? Conn Evt �?                �? Conn Evt �?       (7.5ms interval)
           �? TX/RX    �?                �? TX/RX    �?
           └───────────�?                └───────────�?
           3          5                 15         17

                             ┌─────────────────�?
BR/EDR                       �?   ACL Event    �?             (Poll interval)
ACL                          �?   TX/RX        �?
                             └─────────────────�?
                             9               13

优先级管�?
┌─────────────────────────────────────────────────────────────────────────────�?
�? Priority Level    �? Activity Type           �? Ticker Flag               �?
├────────────────────┼──────────────────────────┼────────────────────────────�?
�? Highest (1)       �? SCO/eSCO                �? TICKER_LAZY_MUST_EXPIRE   �?
�? High (2)          �? BLE Connection Event    �? Normal                    �?
�? Medium (3)        �? BR/EDR ACL              �? Normal                    �?
�? Low (4)           �? BLE Advertising         �? TICKER_LAZY allowed       �?
�? Lowest (5)        �? BR/EDR Inquiry/Page     �? TICKER_LAZY allowed       �?
�?                   �? Scan                    �?                           �?
└─────────────────────────────────────────────────────────────────────────────�?
```

### 7.3 共存时间片计�?

```c
/*
 * 共存参数结构
 */
static struct {
    uint32_t scan_evt_dur;      /* Scan 事件持续时间 (us) */
    uint32_t acl_evt_dur;       /* ACL 事件持续时间 (us) */
    uint8_t  active_modes;      /* 活动模式位图 */
    uint8_t  sco_intv_min;      /* 最�?SCO 间隔 (slots) */
    uint8_t  sco_count;         /* 活动 SCO 链路�?*/
} bredr_slice_params;

/*
 * 时间片计算函�?
 */
static void ticker_bredr_slice_compute(void)
{
    /* 默认�?*/
    bredr_slice_params.scan_evt_dur = 18 * 625;  /* 18 slots */
    bredr_slice_params.acl_evt_dur = 15 * 625;   /* 15 slots */

    /* 根据 SCO 存在调整 */
    if (bredr_slice_params.sco_count > 0) {
        uint8_t t_sco = bredr_slice_params.sco_intv_min;

        if (t_sco == 6) {
            /*
             * Tsco = 6 slots (HV3/EV3)
             * 非常紧凑的时�? 需要大幅缩短其他事�?
             *
             * Timeline:
             * |--SCO--|--gap--|--SCO--|--gap--|
             * 0      2      6      8     12
             *
             * 可用间隙: 4 slots (2.5ms)
             */
            bredr_slice_params.scan_evt_dur = 6 * 625;   /* 6 slots max */
            bredr_slice_params.acl_evt_dur = 3 * 625;    /* 3 slots max */
        } else if (t_sco == 12) {
            /*
             * Tsco = 12 slots (2-EV3)
             * 较宽松的时序
             *
             * Timeline:
             * |--SCO--|----gap----|--SCO--|
             * 0      2          12     14
             *
             * 可用间隙: 10 slots (6.25ms)
             */
            bredr_slice_params.scan_evt_dur = 16 * 625;  /* 16 slots */
            bredr_slice_params.acl_evt_dur = 8 * 625;    /* 8 slots */
        }
    }

    /* 如果同时�?BLE 连接, 进一步调�?*/
    if (ble_conn_count > 0) {
        /* �?BLE 预留时间 */
        uint32_t ble_reserve = 2 * 625;  /* 2 slots for BLE */
        
        if (bredr_slice_params.scan_evt_dur > ble_reserve) {
            bredr_slice_params.scan_evt_dur -= ble_reserve;
        }
    }
}

/*
 * SCO 启动时更新共存参�?
 */
int ticker_bredr_sco_start(uint8_t sco_idx, void *param,
                           uint32_t ticks_anchor, uint8_t t_sco)
{
    /* ... ticker_start() ... */

    /* 更新共存参数 */
    bredr_slice_params.sco_count++;
    if (t_sco < bredr_slice_params.sco_intv_min) {
        bredr_slice_params.sco_intv_min = t_sco;
    }
    ticker_bredr_slice_compute();

    return 0;
}
```

---

## 8. LMP 过程详细实现

### 8.1 连接建立序列

```
┌─────────────────────────────────────────────────────────────────────────────�?
�?                   Connection Establishment Sequence                         �?
└─────────────────────────────────────────────────────────────────────────────�?

Central                                                          Peripheral
   �?                                                                 �?
   �? [Baseband: Page complete, FHS exchanged]                       �?
   �?                                                                 �?
   �? lmp_proc_connection_setup(conn)                                �?
   �? ├── conn->lc_state = LC_MST_WAIT_VERS                          �?
   �? └── ull_bredr_send_pdu_vers_req()                              �?
   �?                                                                 �?
   │──────────────── LMP_version_req ────────────────────────────────►│
   �?                (VersNr, CompId, SubVersNr)                      �?
   �?                                                                 �?
   │◄─────────────── LMP_version_res ─────────────────────────────────�?
   �?                                                                 �?
   �? lmp_proc_version_rx()                                          �?
   �? ├── 保存远端版本信息�?conn->info                              �?
   �? ├── conn->lc_state = LC_WAIT_REM_FEATS                         �?
   �? └── ull_bredr_send_pdu_feats_req()                             �?
   �?                                                                 �?
   │──────────────── LMP_features_req ───────────────────────────────►│
   �?                                                                 �?
   │◄─────────────── LMP_features_res ────────────────────────────────�?
   �?                (Features[8])                                    �?
   �?                                                                 �?
   �? lmp_proc_features_rx()                                         �?
   �? ├── 保存远端特性到 conn->info.remote_features[0]               �?
   �? ├── 检查是否支持扩展特�?                                      �?
   �? �?  if (remote_features & FEAT_EXT_FEATURES_BIT) {             �?
   �? �?      conn->lc_state = LC_WAIT_REM_EXT_FEATS                 �?
   �? �?      ull_bredr_send_pdu_feats_ext_req(page=1)               �?
   �? �?  }                                                          �?
   �? └── 否则继续连接建立                                           �?
   �?                                                                 �?
   │──────────────── LMP_features_req_ext (page=1) ──────────────────►│
   �?                                                                 �?
   │◄─────────────── LMP_features_res_ext ────────────────────────────�?
   �?                                                                 �?
   �? [如果需要更多页, 继续请求 page=2]                              �?
   �?                                                                 �?
   �? lmp_proc_hl_connect()                                          �?
   �? ├── conn->lc_state = LC_WAIT_REM_HL_CON                        �?
   �? └── ull_bredr_send_pdu_host_conn_req()                         �?
   �?                                                                 �?
   │──────────────── LMP_host_connection_req ────────────────────────►│
   �?                                                                 �?
   │◄─────────────── LMP_accepted ────────────────────────────────────�?
   �?                                                                 �?
   �? lmp_proc_setup_complete()                                      �?
   �? └── ull_bredr_send_pdu_setup_cmp()                             �?
   �?                                                                 �?
   │──────────────── LMP_setup_complete ─────────────────────────────►│
   �?                                                                 �?
   │◄─────────────── LMP_setup_complete ──────────────────────────────�?
   �?                                                                 �?
   �? lmp_proc_con_cmp()                                             �?
   �? ├── conn->lc_state = LC_CONNECTED                              �?
   �? ├── conn->link.setup_complete = true                           �?
   �? └── hci_bredr_evt_connection_complete(SUCCESS, handle, ...)    �?
   �?                                                                 �?
   �? [Connection Established - Ready for data transfer]             �?
   �?                                                                 �?
```

### 8.2 认证序列实现

```c
/*
 * 认证发起 (Verifier 角色)
 */
int lmp_proc_auth_initiate(struct ull_bredr_conn *conn)
{
    /* 生成随机�?AU_RAND */
    lll_csrand_get(conn->enc.random_tx, 16);
    
    /* 记录本地请求 */
    conn->req.loc_auth_req = true;
    
    /* 设置状�?*/
    conn->lc_state = LC_WAIT_SRES_RSP;
    
    /* 发�?LMP_au_rand */
    ull_bredr_send_pdu_au_rand(conn_idx, conn->enc.random_tx, LMP_TID_INITIATOR);
    
    /* 启动 LMP 超时定时�?*/
    lmp_proc_start_lmp_to(conn);
    
    return 0;
}

/*
 * 收到 LMP_au_rand (Claimant 角色)
 */
static void lmp_proc_au_rand_rx(struct ull_bredr_conn *conn,
                                uint8_t *pdu, uint8_t tid)
{
    /* 保存收到�?AU_RAND */
    memcpy(conn->enc.random_rx, pdu, 16);
    
    /* 检查是否有 Link Key */
    if (!conn->enc.link_key_valid) {
        /* 没有 Link Key, 需要先配对 */
        ull_bredr_send_pdu_not_acc(conn_idx, LMP_AU_RAND,
                                   BT_HCI_ERR_PIN_OR_KEY_MISSING, tid);
        return;
    }
    
    /* 计算 SRES */
    /* SRES = E1(Link_Key, AU_RAND, BD_ADDR) */
    lmp_proc_auth_compute_sres(conn->enc.lt_key,      /* Link Key */
                               conn->enc.random_rx,    /* AU_RAND */
                               conn->info.bd_addr,     /* BD_ADDR */
                               conn->enc.sres,         /* Output: SRES */
                               conn->enc.aco);         /* Output: ACO */
    
    /* 发�?LMP_sres */
    ull_bredr_send_pdu_sres(conn_idx, conn->enc.sres, tid);
}

/*
 * 收到 LMP_sres (Verifier 角色)
 */
static void lmp_proc_sres_rx(struct ull_bredr_conn *conn,
                             uint8_t *pdu, uint8_t tid)
{
    /* 停止 LMP 超时 */
    lmp_proc_stop_lmp_to(conn);
    
    /* 计算期望�?SRES */
    lmp_proc_auth_compute_sres(conn->enc.lt_key,
                               conn->enc.random_tx,
                               conn->info.bd_addr,
                               conn->enc.sres_expected,
                               conn->enc.aco);
    
    /* 比较 SRES */
    if (memcmp(pdu, conn->enc.sres_expected, 4) == 0) {
        /* 认证成功 */
        conn->req.loc_auth_req = false;
        
        /* 发�?HCI 事件 */
        hci_bredr_evt_authentication_complete(BT_HCI_ERR_SUCCESS,
                                              conn->lll.handle);
        
        /* 返回 CONNECTED 状�?*/
        conn->lc_state = LC_CONNECTED;
        
        /* 如果需要加�? 继续加密过程 */
        if (conn->req.loc_enc_req) {
            lmp_proc_encryption_start(conn);
        }
    } else {
        /* 认证失败 */
        hci_bredr_evt_authentication_complete(BT_HCI_ERR_AUTH_FAIL,
                                              conn->lll.handle);
        
        /* 断开连接 */
        lmp_proc_detach(conn, BT_HCI_ERR_AUTH_FAIL);
    }
}

/*
 * E1 算法实现 (SRES/ACO 计算)
 * Core Spec Vol 2, Part H, Section 6.1
 */
int lmp_proc_auth_compute_sres(uint8_t *link_key, uint8_t *au_rand,
                               uint8_t *bd_addr, uint8_t *sres, uint8_t *aco)
{
    uint8_t hash_input[32];
    uint8_t hash_output[16];
    
    /* 构建输入: Link_Key || AU_RAND */
    memcpy(hash_input, link_key, 16);
    memcpy(hash_input + 16, au_rand, 16);
    
    /* 使用 SAFER+ 算法计算 (简化为 AES-CMAC 示例) */
    /* 实际实现需要使�?E1 算法 */
    bt_crypto_e1(link_key, au_rand, bd_addr, sres, aco);
    
    return 0;
}
```

### 8.3 LMP PDU 发送流�?

```
┌─────────────────────────────────────────────────────────────────────────────�?
�?                        LMP PDU TX Flow                                      �?
└─────────────────────────────────────────────────────────────────────────────�?

ULL Layer (e.g., lmp_proc_auth_initiate)
      �?
      �?ull_bredr_send_pdu_au_rand(conn_idx, random, tid)
      �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[1] LMP PDU Construction: ull_bredr_send_lmp()                              �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?分配 LMP TX 节点:                                                        �?
�?   lmp_tx = mem_acquire(&mem_lmp_tx_pool);                                  �?
�?                                                                            �?
�? �?构建 LMP PDU:                                                            �?
�?   lmp_tx->opcode = LMP_AU_RAND;                                            �?
�?   lmp_tx->tid = tid;                                                       �?
�?   memcpy(lmp_tx->data, random, 16);                                        �?
�?   lmp_tx->len = 17;  /* opcode + 16 bytes random */                        �?
�?                                                                            �?
�? �?加入 LMP TX 队列:                                                        �?
�?   sys_slist_append(&conn->lmp_tx_pending, &lmp_tx->node);                  �?
�?                                                                            �?
�? �?标记�?LMP 待发�?                                                       �?
�?   conn->lmp_tx_ready = true;                                               �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?[Connection Ticker Expires]
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[2] LLL Prepare: lll_bredr_conn_prepare()                                   �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?检�?LMP TX 队列:                                                        �?
�?   if (!sys_slist_is_empty(&conn->lmp_tx_pending)) {                        �?
�?       /* LMP 优先�?ACL 数据 */                                            �?
�?       lmp_tx = sys_slist_peek_head(&conn->lmp_tx_pending);                 �?
�?       tx_pdu_type = PDU_TYPE_LMP;                                          �?
�?   } else if (memq_peek(conn->lll.memq_tx.head)) {                          �?
�?       /* ACL 数据 */                                                       �?
�?       tx_pdu_type = PDU_TYPE_ACL;                                          �?
�?   } else {                                                                 �?
�?       /* 无数�? 发�?POLL (Central) �?NULL (Peripheral) */               �?
�?       tx_pdu_type = PDU_TYPE_POLL;                                         �?
�?   }                                                                        �?
�?                                                                            �?
�? �?构建 Baseband PDU:                                                       �?
�?   if (tx_pdu_type == PDU_TYPE_LMP) {                                       �?
�?       /* LMP PDU 使用 LLID = 0x03 */                                       �?
�?       pdu[0] = (BREDR_LLID_LMP << 0) |                                     �?
�?                (conn->lll.flow << 2) |                                     �?
�?                ((lmp_tx->len & 0x1F) << 3);                                �?
�?       pdu[1] = ((lmp_tx->len >> 5) & 0x1F);                                �?
�?       /* LMP header: TID + Opcode */                                       �?
�?       pdu[2] = (lmp_tx->tid & 0x01) | ((lmp_tx->opcode & 0x7F) << 1);      �?
�?       memcpy(&pdu[3], lmp_tx->data, lmp_tx->len - 1);                      �?
�?   }                                                                        �?
�?                                                                            �?
�? �?配置无线电发�?                                                          �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?[Radio TX Complete]
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[3] Radio ISR: lll_bredr_conn_isr()                                         �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?TX 完成, 等待 ACK                                                        �?
�? �?切换�?RX 模式接收响应                                                   �?
�? �?如果收到 ACK (ARQN=1):                                                   �?
�?   lmp_tx_acked = true;                                                     �?
└─────────────────────────────────┬───────────────────────────────────────────�?
                                  �?
                                  �?
┌─────────────────────────────────────────────────────────────────────────────�?
�?[4] ULL Done: ull_bredr_conn_done()                                         �?
├─────────────────────────────────────────────────────────────────────────────�?
�? �?处理 LMP TX ACK:                                                         �?
�?   if (lmp_tx_acked && tx_pdu_type == PDU_TYPE_LMP) {                       �?
�?       /* 从队列移除已确认�?LMP PDU */                                     �?
�?       lmp_tx = sys_slist_get(&conn->lmp_tx_pending);                       �?
�?       mem_release(lmp_tx, &mem_lmp_tx_pool);                               �?
�?                                                                            �?
�?       /* 如果是需要响应的 LMP, 启动超时 */                                 �?
�?       if (lmp_needs_response(lmp_tx->opcode)) {                            �?
�?           lmp_proc_start_lmp_to(conn);                                     �?
�?       }                                                                    �?
�?   }                                                                        �?
└─────────────────────────────────────────────────────────────────────────────�?
```

---

## 9. Inquiry/Page 状态机

### 9.1 Inquiry 状态机

```
┌─────────────────────────────────────────────────────────────────────────────�?
�?                        Inquiry State Machine                                �?
└─────────────────────────────────────────────────────────────────────────────�?

                              ┌──────────────�?
                              �? INQ_IDLE    �?
                              └──────┬───────�?
                                     �?HCI_Inquiry command
                                     �?ull_bredr_inquiry_start()
                                     �?
                              ┌──────────────�?
                              �?INQ_TRAIN_A  │◄─────────────────�?
                              �?             �?                  �?
                              └──────┬───────�?                  �?
                                     �?                          �?
                                     �?16 frequencies scanned    �?
                                     �?                          �?
                              ┌──────────────�?                  �?
                              �?INQ_TRAIN_B  �?                  �?
                              �?             �?                  �?
                              └──────┬───────�?                  �?
                                     �?                          �?
                                     �?16 frequencies scanned    �?
                                     �?                          �?
                                     ├───────────────────────────�?
                                     �?(Repeat until timeout or max responses)
                                     �?
         ┌───────────────────────────┼───────────────────────────�?
         �?                          �?                          �?
         �?                          �?                          �?
┌─────────────────�?     ┌─────────────────�?     ┌─────────────────�?
�?FHS Received    �?     �?Inquiry Timeout �?     �?Max Responses   �?
�?                �?     �?                �?     �?Reached         �?
└────────┬────────�?     └────────┬────────�?     └────────┬────────�?
         �?                       �?                       �?
         �?Generate               �?                       �?
         �?Inquiry Result         �?                       �?
         �?Event                  �?                       �?
         �?                       �?                       �?
         └────────────────────────┴────────────────────────�?
                                  �?
                                  �?
                         ┌──────────────────�?
                         �?INQ_COMPLETE     �?
                         �?                 �?
                         �?Generate         �?
                         �?Inquiry Complete �?
                         �?Event            �?
                         └────────┬─────────�?
                                  �?
                                  �?
                         ┌──────────────────�?
                         �?   INQ_IDLE      �?
                         └──────────────────�?

Inquiry 跳频序列:
┌─────────────────────────────────────────────────────────────────────────────�?
�? Train A: f(k) for k = 0..15  (基于 GIAC/LIAC LAP)                          �?
�? Train B: f(k) for k = 16..31                                               �?
�?                                                                             �?
�? 每个 train �?256 �?slot (160ms) 内完�?                                  �?
�? �?inquiry 时间 = inquiry_length * 1.28s                                   �?
└─────────────────────────────────────────────────────────────────────────────�?
```

### 9.2 Page 状态机

```
┌─────────────────────────────────────────────────────────────────────────────�?
�?                          Page State Machine                                 �?
└─────────────────────────────────────────────────────────────────────────────�?

                              ┌──────────────�?
                              �? PAGE_IDLE   �?
                              └──────┬───────�?
                                     �?HCI_Create_Connection
                                     �?ull_bredr_page_start()
                                     �?
                              ┌──────────────�?
                              │PAGE_TRAIN_A  │◄─────────────────�?
                              �?(ID packets) �?                  �?
                              └──────┬───────�?                  �?
                                     �?                          �?
                                     �?16 ID packets sent        �?
                                     �?                          �?
                              ┌──────────────�?                  �?
                              │PAGE_TRAIN_B  �?                  �?
                              �?(ID packets) �?                  �?
                              └──────┬───────�?                  �?
                                     �?                          �?
                                     �?16 ID packets sent        �?
                                     �?                          �?
                                     ├───────────────────────────�?
                                     �?(Repeat Npage times)
                                     �?
         ┌───────────────────────────┼───────────────────────────�?
         �?                          �?                          �?
         �?                          �?                          �?
┌─────────────────�?     ┌─────────────────�?                   �?
�?Page Response   �?     �?Page Timeout    �?                   �?
�?Received        �?     �?                �?                   �?
└────────┬────────�?     └────────┬────────�?                   �?
         �?                       �?                             �?
         �?                       �?                             �?
┌─────────────────�?     ┌─────────────────�?                   �?
│PAGE_RESP_WAIT   �?     �?Connection      �?                   �?
�?(Send FHS)      �?     �?Complete Event  �?                   �?
└────────┬────────�?     �?(Page Timeout)  �?                   �?
         �?              └─────────────────�?                   �?
         �?FHS sent, wait for ID                                �?
         �?                                                      �?
┌─────────────────�?                                             �?
│PAGE_RESP_ID     �?                                             �?
�?(Wait ID ack)   �?                                             �?
└────────┬────────�?                                             �?
         �?                                                      �?
         �?ID received                                           �?
         �?                                                      �?
┌─────────────────�?                                             �?
�?PAGE_COMPLETE   �?                                             �?
�?                �?                                             �?
�?Connection      �?                                             �?
�?Established!    �?                                             �?
└────────┬────────�?                                             �?
         �?                                                      �?
         �?Start connection ticker                               �?
         �?Begin LMP connection setup                            �?
         �?                                                      �?
┌─────────────────�?                                             �?
�? PAGE_IDLE      │◄─────────────────────────────────────────────�?
└─────────────────�?

Page 时序参数:
┌─────────────────────────────────────────────────────────────────────────────�?
�? Page Scan Repetition Mode �?Npage  �?Page Timeout                          �?
�? ─────────────────────────────────────────────────────────────────────────  �?
�? R0                        �?1      �?1.28s                                 �?
�? R1                        �?128    �?1.28s * 128 = 163.84s                 �?
�? R2                        �?256    �?1.28s * 256 = 327.68s                 �?
└─────────────────────────────────────────────────────────────────────────────�?
```

---

## 10. 数据结构详细说明

### 10.1 连接上下文完整结�?

```c
/*
 * BR/EDR 连接上下�?(基于 RW lc_env_tag)
 */
struct ull_bredr_conn {
    /*=== 基础结构 ===*/
    struct ull_hdr ull;              /* ULL 通用�?(ref count, timing) */
    struct lll_bredr_conn lll;       /* LLL 层数�?*/
    uint8_t ticker_id;               /* 分配�?Ticker ID */
    uint8_t lc_state;                /* Link Controller 状态机状�?*/

    /*=== 链路参数 (ull_bredr_link) ===*/
    struct {
        /* QoS 参数 */
        uint32_t token_rate;
        uint32_t peak_bandwidth;
        uint32_t latency;
        uint32_t delay_variation;
        uint32_t access_latency;
        uint32_t token_bucket_size;
        uint32_t switch_instant;

        /* 链路参数 */
        uint16_t link_timeout;           /* LSTO */
        uint16_t acl_packet_type;        /* 允许的包类型 */
        uint16_t cur_packet_type;        /* 当前使用的包类型 */
        uint16_t link_policy_settings;   /* Hold/Sniff/Park 策略 */
        uint16_t poll_interval;          /* Tpoll */
        uint16_t slot_offset;
        uint16_t failed_contact;
        uint16_t rx_preferred_rate;
        uint16_t auth_payl_to;           /* 认证 payload 超时 */

        /* 设备�?*/
        uint8_t  class_of_device[3];

        /* Slot 管理 */
        uint8_t  tx_max_slot_cur;        /* 当前最�?TX slot */
        uint8_t  max_slot_received;      /* 对端通告的最�?slot */

        /* 角色和状�?*/
        uint8_t  allow_role_switch;
        uint8_t  role;                   /* CENTRAL/PERIPHERAL */
        uint8_t  reason;                 /* 断开原因 */
        uint8_t  lt_addr;                /* Logical Transport Address */

        /* 事务 ID */
        uint8_t  rx_tr_id_server;
        uint8_t  rx_tr_id_client;

        /* 模式和流�?*/
        uint8_t  current_mode;           /* Active/Hold/Sniff/Park */
        uint8_t  flow_direction;
        uint8_t  service_type;
        uint8_t  cur_packet_type_table;  /* 1Mbps / 2-3Mbps */

        /* 状态标�?*/
        bool     initiator;
        bool     connected_state;
        bool     setup_complete;
        bool     setup_comp_rx;
        bool     setup_comp_tx;
        bool     connection_complete_sent;
        bool     host_connected;
        bool     epc_supported;          /* Enhanced Power Control */
    } link;

    /*=== 远端设备信息 (ull_bredr_info) ===*/
    struct {
        uint8_t  remote_features[3][8];  /* 3 pages of features */
        uint8_t  remote_feat_rec;        /* 已接收的 feature pages 位图 */
        uint8_t  local_bd_addr[6];
        uint8_t  bd_addr[6];             /* 远端 BD_ADDR */
        uint8_t  remote_vers;            /* LMP 版本 */
        uint16_t remote_comp_id;         /* 厂商 ID */
        uint16_t remote_subvers;         /* 子版�?*/
        bool     recv_rem_ver_rec;       /* 已接收版本信�?*/
        uint8_t  remote_name[248];       /* 远端名称 */
        uint8_t  remote_name_len;
    } info;

    /*=== 加密状�?(ull_bredr_enc) ===*/
    struct {
        uint8_t  key_from_host;
        uint16_t key_size_mask;
        uint8_t  key_type;
        uint8_t  key_flag;
        uint8_t  new_key_flag;
        uint8_t  key_status;
        uint8_t  pin_status;
        uint8_t  pin_length;
        uint8_t  enc_size;               /* 加密密钥大小 */
        uint8_t  enc_mode;               /* 当前加密模式 */
        uint8_t  new_enc_mode;           /* 请求的加密模�?*/
        uint8_t  enc_enable;

        /* 密钥 */
        uint8_t  auth_key[16];
        uint8_t  lt_key[16];             /* Link Key */
        uint8_t  semi_permanent_key[16];
        uint8_t  overlay[16];
        uint8_t  random_rx[16];          /* 收到的随机数 */
        uint8_t  random_tx[16];          /* 发送的随机�?*/
        uint8_t  enc_key[16];            /* 加密密钥 */

        /* 认证 */
        uint8_t  sres[4];
        uint8_t  sres_expected[4];
        uint8_t  aco[12];                /* Authenticated Ciphering Offset */
        uint8_t  pin_code[16];

        /* 标志 */
        bool     link_key_valid;
        bool     prevent_enc_evt;
    } enc;

    /*=== 请求状�?(ull_bredr_req) ===*/
    struct {
        /* 本地请求 */
        bool loc_name_req;
        bool loc_remote_extended_req;
        bool loc_detach_req;
        bool loc_cpt_req;
        bool loc_enc_req;
        bool loc_auth_req;
        bool loc_key_exchange_req;
        bool loc_enc_key_refresh;
        bool loc_switch_req;
        bool loc_vers_req;
        bool loc_flow_spec_req;

        /* 对端请求 */
        bool peer_switch_req;
        bool peer_auth_req;
        bool peer_enc_req;
        bool peer_detach_req;
        bool peer_enc_key_refresh;

        /* 内部请求 */
        bool restart_enc_req;
        bool master_key_req;
    } req;

    /*=== AFH 参数 (ull_bredr_afh) ===*/
    struct {
        uint8_t  ch_map[10];             /* 当前信道映射 */
        uint8_t  ch_class[10];           /* 信道分类 */
        uint32_t reporting_interval;
        bool     en;                     /* AFH 启用 */
        bool     temp_en;
        bool     reporting_en;
        bool     lmp_ch_class_pending;
    } afh;

    /*=== SSP 参数 (ull_bredr_sp) ===*/
    struct {
        uint32_t passkey;
        uint8_t  loc_rand_n[16];         /* 本地随机�?*/
        uint8_t  rem_rand_n[16];         /* 远端随机�?*/
        uint8_t  loc_commitment[16];     /* 本地承诺�?*/
        uint8_t  rem_commitment[16];     /* 远端承诺�?*/
        uint8_t  dhkey_check[16];
        uint8_t  io_cap_loc[3];          /* 本地 IO 能力 */
        uint8_t  io_cap_rem[3];          /* 远端 IO 能力 */
        uint8_t  encap_pdu_ctr;          /* 封装 PDU 计数 */
        uint8_t  sp_phase1_failed;
        uint8_t  sp_dhkey;
        uint8_t  sp_tid;
        bool     sp_initiator;
        bool     sec_con;                /* Secure Connections */
    } sp;

    /*=== EPR 参数 (ull_bredr_epr) ===*/
    struct {
        bool     on;
        bool     rsw;                    /* Role Switch pending */
        uint8_t  rsw_error;
        bool     cclk;
    } epr;

    /*=== 本地事务 (ull_bredr_local_trans) ===*/
    struct {
        uint8_t  opcode;
        uint8_t  opcode_ext;
        uint8_t  in_use;                 /* LC_UTIL_NOT_USED / LC_UTIL_INUSED */
    } local_trans;

    /*=== SAM 参数 (ull_bredr_sam) ===*/
    struct {
        uint32_t instant;
        uint16_t t_sam;
        uint16_t n_tx_slots;
        uint16_t n_rx_slots;
        /* ... 更多 SAM 参数 ... */
    } sam_info;

    /*=== LMP 事务队列 ===*/
    sys_slist_t lmp_tx_pending;          /* 待发�?LMP PDU 队列 */
    sys_slist_t lmp_rx_pending;          /* 待处�?LMP PDU 队列 */

    /*=== Sniff 参数 ===*/
    uint16_t sniff_interval;
    uint16_t sniff_attempt;
    uint16_t sniff_timeout;
    uint16_t sniff_offset;

    /*=== TX 队列 ===*/
    struct {
        void *head;
        void *tail;
    } tx_queue;

    /*=== 监督 ===*/
    uint32_t supervision_expire;

    /*=== 回调 ===*/
    void (*disconnect_cb)(uint8_t reason);
};
```

---

## 11. 文件结构与模块职�?

```
subsys/bluetooth/controller/ll_sw/bredr/
�?
├── CMakeLists.txt              # 构建配置
├── Kconfig                     # 配置选项
�?
├── doc/
�?  └── BR_EDR_Controller_Design.md  # 本设计文�?
�?
├── pdu_bredr.h                 # PDU 结构定义
�?  ├── Baseband 包头结构
�?  ├── FHS 包结�?
�?  ├── ACL payload �?
�?  ├── LMP opcode 定义
�?  └── LMP PDU 结构
�?
├── lll_bredr.h                 # LLL 层接�?
�?  ├── LLL 上下文结�?(inquiry, page, conn, sco, esco)
�?  ├── 跳频函数声明
�?  ├── 访问码生成函数声�?
�?  └── Radio 事件处理函数声明
�?
├── lll_bredr.c                 # LLL 层实�?
�?  ├── 跳频算法实现
�?  ├── AFH 信道计算
�?  ├── 访问码生�?(DAC, CAC, IAC)
�?  ├── Radio prepare 函数
�?  ├── Radio ISR 处理
�?  └── E0 加密支持
�?
├── ull_bredr.h                 # ULL 层接�?
�?  ├── 连接上下文结�?(完整)
�?  ├── LM 环境结构
�?  ├── 状态机枚举
�?  ├── ULL API 声明
�?  └── LMP 发送函数声�?
�?
├── ull_bredr.c                 # ULL 层实�?
�?  ├── 初始�?重置
�?  ├── Inquiry/Page 管理
�?  ├── 连接池管�?
�?  ├── LT_ADDR 分配
�?  ├── AFH 管理
�?  ├── SAM 管理
�?  ├── Done 处理
�?  └── RX 分发
�?
├── lmp_proc.h                  # LMP 过程接口
�?  ├── LMP 过程状态枚�?
�?  ├── 认证/加密状�?
�?  ├── SSP 状�?
�?  ├── Feature bit 定义
�?  └── LMP 过程函数声明
�?
├── lmp_proc.c                  # LMP 过程实现
�?  ├── 连接建立序列
�?  ├── 版本/特性交�?
�?  ├── 认证过程 (E1 算法)
�?  ├── 加密过程 (E3 算法)
�?  ├── SSP 过程 (P-192/P-256)
�?  ├── 电源模式 (Sniff/Hold/Park)
�?  ├── 角色切换
�?  ├── QoS 设置
�?  ├── SCO/eSCO 协商
�?  └── 冲突管理
�?
├── ticker_bredr.h              # Ticker 集成接口
�?  ├── Ticker 常量定义
�?  ├── Ticker 启动/停止函数声明
�?  └── 共存参数获取函数
�?
├── ticker_bredr.c              # Ticker 集成实现
�?  ├── 共存参数计算
�?  ├── Inquiry ticker
�?  ├── Inquiry Scan ticker
�?  ├── Page ticker
�?  ├── Page Scan ticker
�?  ├── ACL Connection ticker
�?  ├── SCO ticker (MUST_EXPIRE)
�?  └── eSCO ticker (MUST_EXPIRE)
�?
├── hci_bredr.h                 # HCI 接口
�?  ├── HCI 命令处理函数声明
�?  └── HCI 事件生成函数声明
�?
└── hci_bredr.c                 # HCI 实现
    ├── Link Control 命令 (OGF 0x01)
    ├── Link Policy 命令 (OGF 0x02)
    ├── Controller & Baseband 命令 (OGF 0x03)
    ├── Informational 命令 (OGF 0x04)
    ├── Status 命令 (OGF 0x05)
    ├── Testing 命令 (OGF 0x06)
    └── 事件生成函数
```

---

## 12. 参考文�?

- Bluetooth Core Specification v6.1
  - Vol 2, Part B: Baseband Specification
  - Vol 2, Part C: Link Manager Protocol Specification
  - Vol 2, Part H: Security Specification
  - Vol 4, Part E: Host Controller Interface
- Zephyr BLE Controller 架构文档
- RivieraWaves Controller 参考实�?(lb, lc, lm 模块)
