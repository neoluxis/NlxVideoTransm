import cv2
import socket
import struct
import time
import serial
from concurrent.futures import ThreadPoolExecutor
import threading
import queue

FRAME_WIDTH  = 2*1280
FRAME_HEIGHT = 720
SMALL_QUALITY = 35
LARGE_QUALITY = 100
SERVER_IP    = '192.168.123.52'
PORT         = 5001

SERIAL_PORT  = '/dev/ttyS1'
BAUDRATE     = 115200
TIMEOUT      = 0.01

DELIM = b'|PROTOCOL_SWITCH|'
HDR   = struct.pack('>B', 0x02)

executor = ThreadPoolExecutor(max_workers=2)
frame_queue = queue.Queue(maxsize=1)
stop_event = threading.Event()

def send_control(sock, cmd: str):
    c = cmd.encode('utf-8')
    header = DELIM + HDR + struct.pack('>L', len(c)) + c + DELIM
    try:
        sock.sendall(header)
    except Exception as e:
        print(f"[Sender] 发送控制消息异常: {e}")

def async_send_large(sock, frame):
    try:
        send_control(sock, 'image')
        t0 = time.time()
        _, buf = cv2.imencode('.jpg', frame, [int(cv2.IMWRITE_JPEG_QUALITY), LARGE_QUALITY])
        t1 = time.time()
        packet = struct.pack('>L', len(buf)) + buf.tobytes()
        sock.sendall(packet)
        t2 = time.time()
        print(f"[Sender][BIG] Enc: {t1-t0:.02f}, Send: {t2-t1:.02f}")
    except Exception as e:
        print(f"[Sender] 大图发送异常: {e}")

def small_frame_sender(sock, frame_queue, stop_event):
    while not stop_event.is_set():
        try:
            frame = frame_queue.get(timeout=1)
        except queue.Empty:
            continue
        if frame is None:
            break
        try:
            t0 = time.time()
            _, buf = cv2.imencode('.jpg', frame, [int(cv2.IMWRITE_JPEG_QUALITY), SMALL_QUALITY])
            t1 = time.time()
            packet = struct.pack('>L', len(buf)) + buf.tobytes()
            sock.sendall(packet)
            t2 = time.time()
            print(f"[Sender][SMALL] Enc: {t1-t0:.02f}, Send: {t2-t1:.02f}")
        except Exception as e:
            print(f"[Sender] 小流发送异常: {e}")
            stop_event.set()
            break

def tcp_send_video():
    cap = cv2.VideoCapture(2, cv2.CAP_V4L2)
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'MJPG'))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT)
    cap.set(cv2.CAP_PROP_FPS, 30)

    if not cap.isOpened():
        print("[Sender] 打开摄像头失败")
        return

    ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=TIMEOUT)

    sock = None
    sender_thread = None

    while True:
        if sock is None:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                sock.connect((SERVER_IP, PORT))
                print(f"[Sender] 已连接服务器 {SERVER_IP}:{PORT}")
                stop_event.clear()
                sender_thread = threading.Thread(target=small_frame_sender, args=(sock, frame_queue, stop_event))
                sender_thread.start()
            except Exception as e:
                print(f"[Sender] 连接失败，2秒后重试: {e}")
                if sock:
                    try:
                        sock.shutdown(socket.SHUT_RDWR)
                    except:
                        pass
                    sock.close()
                    sock = None
                time.sleep(2)
                continue

        try:
            ret, frame = cap.read()
            if not ret:
                time.sleep(0.01)
                continue

            try:
                frame_queue.put_nowait(frame.copy())
            except queue.Full:
                try:
                    frame_queue.get_nowait()
                    frame_queue.put_nowait(frame.copy())
                except queue.Full:
                    pass

            if ser.in_waiting:
                data = ser.read(1)
                if data == b'S':
                    executor.submit(async_send_large, sock, frame.copy())

            time.sleep(1/30)

            if stop_event.is_set():
                raise RuntimeError("发送线程异常退出，重连中")

        except Exception as e:
            print(f"[Sender] 发送异常: {e}")
            stop_event.set()
            if sock:
                try:
                    sock.shutdown(socket.SHUT_RDWR)
                except:
                    pass
                sock.close()
                sock = None
            frame_queue.put(None)
            if sender_thread:
                sender_thread.join()
                sender_thread = None
            print("[Sender] 连接断开，重连中...")
            time.sleep(2)

if __name__ == '__main__':
    tcp_send_video()
