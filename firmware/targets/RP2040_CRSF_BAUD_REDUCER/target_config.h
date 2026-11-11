/* Настройки таргета RP2040_CRSF_BAUD_REDUCER.
 *
 * Подключается первым (до crsf_baud_config.h), поэтому значения ниже имеют
 * приоритет над общими умолчаниями.
 *
 *   приёмник Crossfire/ELRS  →  GP5 (UART1 RX)
 *   Pico                     →  GP0 (UART0 TX)  →  полётник
 *   полётник                 →  GP1 (UART0 RX)  →  Pico (обратный канал, выключен)
 */

#ifndef CRSF_BAUD_IN
#  define CRSF_BAUD_IN               400000UL   /* базовый CRSF; у ELRS — 420000 */
#endif
#ifndef CRSF_BAUD_OUT
#  define CRSF_BAUD_OUT              200000UL   /* скорость, которую ждёт полётник */
#endif

#ifndef CRSF_BAUD_IN_UART
#  define CRSF_BAUD_IN_UART           2         /* Serial2 = UART1 */
#endif
#ifndef CRSF_BAUD_OUT_UART
#  define CRSF_BAUD_OUT_UART          1         /* Serial1 = UART0 */
#endif

#ifndef CRSF_BAUD_IN_RX_PIN
#  define CRSF_BAUD_IN_RX_PIN         5         /* GP5 */
#endif
#ifndef CRSF_BAUD_IN_TX_PIN
#  define CRSF_BAUD_IN_TX_PIN         4         /* GP4 */
#endif
#ifndef CRSF_BAUD_OUT_TX_PIN
#  define CRSF_BAUD_OUT_TX_PIN        0         /* GP0 */
#endif
#ifndef CRSF_BAUD_OUT_RX_PIN
#  define CRSF_BAUD_OUT_RX_PIN        1         /* GP1 */
#endif

#ifndef CRSF_BAUD_ECHO_ENABLE
#  define CRSF_BAUD_ECHO_ENABLE       0
#endif

#ifndef CRSF_BAUD_QUEUE_SLOTS
#  define CRSF_BAUD_QUEUE_SLOTS       32u
#endif

#ifndef CRSF_BAUD_LOG_BAUD
#  define CRSF_BAUD_LOG_BAUD           115200UL
#endif
#ifndef CRSF_BAUD_LOG_EVERY_FRAMES
#  define CRSF_BAUD_LOG_EVERY_FRAMES  100u
#endif
#ifndef CRSF_BAUD_LED_ENABLE
#  define CRSF_BAUD_LED_ENABLE         1
#endif
#ifndef CRSF_BAUD_LED_PIN
#  define CRSF_BAUD_LED_PIN            25u
#endif
