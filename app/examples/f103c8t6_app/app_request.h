#ifndef APP_REQUEST_H
#define APP_REQUEST_H
/* 升级口最小响应器（external_interface.md §5）：APP 运行期监听升级协议，
 * 支持 PING 与 SET_META(bl_request)；置位请求 → 参数区掉电安全落盘 →
 * 回 OK → 复位，由 BL 消费请求进升级模式。 */
void app_request_init(void);
void app_request_poll(void);   /* 主循环调用：非阻塞 */

#endif /* APP_REQUEST_H */
