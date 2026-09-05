import sensor, image, time, tf, gc
from machine import UART

UART_PORT = 2
UART_BAUD = 115200
MODEL_PATH = "/sd/yolo3_iou_smartcar_final_with_post_processing.tflite"
DETECT_THRESHOLD = 0.70
STATUS_INTERVAL_MS = 300
ZENG_LABEL = 0

# Keep inference camera settings close to the training capture settings.
CAMERA_BRIGHTNESS = -2
CAMERA_CONTRAST = 1
CAMERA_SATURATION = 0
CAMERA_AUTO_EXPOSURE = False
CAMERA_EXPOSURE_US = 500
CAMERA_AUTO_GAIN = False
CAMERA_GAIN_DB = 0
CAMERA_AUTO_WHITEBAL = False

uart = UART(UART_PORT, baudrate=UART_BAUD)


def send_status(text):
    uart.write(text + "\r\n")
    print(text)


def clamp_rect(x, y, w, h, img_w, img_h):
    if x < 0:
        x = 0
    if y < 0:
        y = 0
    if x + w > img_w:
        w = img_w - x
    if y + h > img_h:
        h = img_h - y
    return x, y, w, h


def configure_camera():
    try:
        sensor.set_brightness(CAMERA_BRIGHTNESS)
    except Exception as e:
        print("skip brightness:", e)
    try:
        sensor.set_contrast(CAMERA_CONTRAST)
    except Exception as e:
        print("skip contrast:", e)
    try:
        sensor.set_saturation(CAMERA_SATURATION)
    except Exception as e:
        print("skip saturation:", e)

    try:
        sensor.set_auto_exposure(CAMERA_AUTO_EXPOSURE, exposure_us=CAMERA_EXPOSURE_US)
    except TypeError:
        sensor.set_auto_exposure(CAMERA_AUTO_EXPOSURE)
    except Exception as e:
        print("skip exposure:", e)

    try:
        sensor.set_auto_gain(CAMERA_AUTO_GAIN, gain_db=CAMERA_GAIN_DB)
    except TypeError:
        sensor.set_auto_gain(CAMERA_AUTO_GAIN)
    except Exception as e:
        print("skip gain:", e)

    try:
        sensor.set_auto_whitebal(CAMERA_AUTO_WHITEBAL)
    except Exception as e:
        print("skip whitebal:", e)

    try:
        print("exposure_us:", sensor.get_exposure_us(), "gain_db:", sensor.get_gain_db())
    except Exception:
        pass


try:
    net = tf.load(MODEL_PATH)
except Exception as e:
    print("model load failed:", e)
    while True:
        send_status("FACE:ERR,MODEL")
        time.sleep_ms(1000)

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
configure_camera()
sensor.skip_frames(time=2000)

clock = time.clock()
last_status_tick = time.ticks_ms()
last_fps_tick = time.ticks_ms()

while True:
    clock.tick()
    img = sensor.snapshot()
    best_score = 0

    for obj in tf.detect(net, img):
        x1, y1, x2, y2, label, score = obj
        if int(label) != ZENG_LABEL or score < DETECT_THRESHOLD:
            continue

        score_percent = int(score * 100)
        if score_percent > best_score:
            best_score = score_percent

        img_w = img.width()
        img_h = img.height()
        x = int(x1 * img_w)
        y = int(y1 * img_h)
        w = int((x2 - x1) * img_w)
        h = int((y2 - y1) * img_h)
        x, y, w, h = clamp_rect(x, y, w, h, img_w, img_h)
        img.draw_rectangle((x, y, w, h), color=(255, 0, 0), thickness=2)
        img.draw_string(x, max(0, y - 20), "zeng", color=(255, 0, 0), scale=2)

    now = time.ticks_ms()
    if time.ticks_diff(now, last_status_tick) >= STATUS_INTERVAL_MS:
        if best_score > 0:
            send_status("FACE:ZENG,%d" % best_score)
        else:
            send_status("FACE:NONE")
        last_status_tick = now

    if time.ticks_diff(now, last_fps_tick) >= 1000:
        print("fps:", clock.fps())
        last_fps_tick = now
        gc.collect()
