import socket
import struct
import threading
import queue
import numpy as np
import cv2
from concurrent.futures import ThreadPoolExecutor
import time
import os

SERVER_IP = '0.0.0.0'
PORT = 5001
BUFFER_SIZE = 4096
DELIM = b'|PROTOCOL_SWITCH|'

frame_queue = queue.Queue(maxsize=1)
decode_executor = ThreadPoolExecutor(max_workers=2)
save_executor = ThreadPoolExecutor(max_workers=2)
stop_event = threading.Event()

def save_large_image(data, idx):
    arr = np.frombuffer(data, np.uint8)
    img = cv2.imdecode(arr, cv2.IMREAD_COLOR)
    if img is not None:
        os.makedirs('received_data/image', exist_ok=True)
        path = f'received_data/image/{idx}.jpg'
        cv2.imwrite(path, img, [int(cv2.IMWRITE_JPEG_QUALITY), 95])
        print(f"[Save] Image saved: {path}")

def decode_and_enqueue(jpeg_data):
    arr = np.frombuffer(jpeg_data, np.uint8)
    frame = cv2.imdecode(arr, cv2.IMREAD_COLOR)
    if frame is not None:
        while not frame_queue.empty():
            try:
                frame_queue.get_nowait()
            except queue.Empty:
                break
        frame_queue.put(frame)

def display_loop():
    while not stop_event.is_set():
        try:
            frame = frame_queue.get(timeout=1)
        except queue.Empty:
            continue
        if frame is None:
            break
        cv2.imshow('Received Stream', frame)
        if cv2.waitKey(1) & 0xFF == 27:
            stop_event.set()
            break
    cv2.destroyAllWindows()

def handle_client(conn, addr):
    print(f"[Server] 新连接来自 {addr}")
    buffer = b''
    image_counter = 1
    try:
        while not stop_event.is_set():
            data = conn.recv(BUFFER_SIZE)
            if not data:
                print(f"[Server] 客户端断开: {addr}")
                break
            buffer += data

            # 处理协议分隔符消息（控制指令）
            while True:
                if buffer.startswith(DELIM):
                    # 简单示范解析控制协议（实际根据协议设计完善）
                    try:
                        # DELIM + HDR(1) + len(4) + payload + DELIM
                        if len(buffer) < len(DELIM) + 1 + 4:
                            break
                        hdr = buffer[len(DELIM)]
                        length = struct.unpack('>L', buffer[len(DELIM)+1:len(DELIM)+5])[0]
                        if len(buffer) < len(DELIM)+5+length+len(DELIM):
                            break
                        payload = buffer[len(DELIM)+5:len(DELIM)+5+length]
                        # 处理控制消息
                        cmd = payload.decode('utf-8')
                        print(f"[Server] 控制消息: {cmd}")

                        buffer = buffer[len(DELIM)+5+length+len(DELIM):]
                        if cmd == 'image':
                            # 后面紧跟一个大图包，下一轮循环处理
                            pass
                        else:
                            # 其他控制消息忽略
                            pass
                    except Exception as e:
                        print(f"[Server] 控制消息解析异常: {e}")
                        break
                else:
                    if len(buffer) < 4:
                        break
                    L = struct.unpack('>L', buffer[:4])[0]
                    if L > 10 * 1024 * 1024:
                        print(f"[Server] 数据包太大，丢弃 {L} bytes")
                        buffer = b''
                        break
                    if len(buffer) < 4 + L:
                        break
                    jpeg_data = buffer[4:4+L]
                    buffer = buffer[4+L:]
                    decode_executor.submit(decode_and_enqueue, jpeg_data)
    except Exception as e:
        print(f"[Server] 连接异常 {addr}: {e}")
    finally:
        conn.close()
        print(f"[Server] 连接关闭 {addr}")

def tcp_server():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    try:
        sock.bind((SERVER_IP, PORT))
    except Exception as e:
        print(f"[Server] 绑定端口失败: {e}")
        return

    sock.listen(5)
    print(f"[Server] 监听中 {SERVER_IP}:{PORT}")

    display_thread = threading.Thread(target=display_loop, daemon=True)
    display_thread.start()

    while not stop_event.is_set():
        try:
            conn, addr = sock.accept()
            client_thread = threading.Thread(target=handle_client, args=(conn, addr), daemon=True)
            client_thread.start()
        except Exception as e:
            print(f"[Server] accept异常: {e}")
            time.sleep(1)

    sock.close()
    stop_event.set()
    frame_queue.put(None)
    display_thread.join()

if __name__ == '__main__':
    tcp_server()
