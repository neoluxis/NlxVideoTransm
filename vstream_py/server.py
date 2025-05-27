import cv2
import socket
import struct
import time
import serial
import threading
import argparse
from concurrent.futures import ThreadPoolExecutor

# ---------- 线程池 ----------
executor = ThreadPoolExecutor(max_workers=2)
clients = []
clients_lock = threading.Lock()

def send_frame(clients, frame, quality):
    """发送JPEG图像帧，并返回编码与发送耗时"""
    encode_start = time.time()
    _, buf = cv2.imencode('.jpg', frame, [int(cv2.IMWRITE_JPEG_QUALITY), quality])
    encode_time = time.time() - encode_start

    packet = struct.pack('>L', len(buf)) + buf.tobytes()

    send_start = time.time()
    with clients_lock:
        for client in clients[:]:
            try:
                client.sendall(packet)
            except (ConnectionError, BrokenPipeError):
                print(f"[Sender] 客户端 {client.getpeername()} 断开")
                clients.remove(client)
    send_time = time.time() - send_start

    return encode_time, send_time

def async_send_snapshot(clients, frame, quality):
    """发送快照，并打印耗时信息"""
    total_start = time.time()
    encode_time, send_time = send_frame(clients, frame, quality)
    total_time = time.time() - total_start
    print(f"[Snapshot] 编码: {encode_time:.3f}s | 发送: {send_time:.3f}s | 总计: {total_time:.3f}s")

def handle_client(client, addr):
    """处理客户端连接"""
    print(f"[Sender] 新客户端: {addr}")
    try:
        while True:
            data = client.recv(1024)
            if not data:
                break
    except (ConnectionError, ConnectionResetError):
        print(f"[Sender] 客户端 {addr} 断开")
    finally:
        with clients_lock:
            if client in clients:
                clients.remove(client)
        client.close()

def run_sender_server(args):
    cap = cv2.VideoCapture(args.camera, cv2.CAP_V4L2)
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'MJPG'))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, args.width)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, args.height)
    cap.set(cv2.CAP_PROP_FPS, 30)

    if not cap.isOpened():
        print("[Sender] 摄像头打开失败")
        return

    ser = None
    if args.serial:
        try:
            ser = serial.Serial(args.serial, args.baudrate, timeout=0.01)
        except serial.SerialException as e:
            print(f"[Sender] 串口打开失败: {e}")

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((args.ip, args.port))
        s.listen(5)
        print(f"[Sender] 监听 {args.ip}:{args.port}")

        def accept_clients():
            while True:
                try:
                    client, addr = s.accept()
                    with clients_lock:
                        clients.append(client)
                    threading.Thread(target=handle_client, args=(client, addr), daemon=True).start()
                except Exception as e:
                    print(f"[Sender] 接受客户端异常: {e}")
                    break

        threading.Thread(target=accept_clients, daemon=True).start()

        try:
            while True:
                frame_start = time.time()
                ret, frame = cap.read()
                if not ret:
                    continue
                capture_time = time.time() - frame_start

                # 小图流处理
                small = cv2.resize(frame, (args.small_width, args.small_height))
                encode_time, send_time = send_frame(clients, small, args.quality_small)
                total_time = time.time() - frame_start

                print(f"[Frame] 拍照: {capture_time:.3f}s | 编码: {encode_time:.3f}s | 发送: {send_time:.3f}s | 总计: {total_time:.3f}s")

                # 快照触发检测
                if ser and ser.in_waiting:
                    data = ser.read()
                    if b'S' == data:
                        executor.submit(async_send_snapshot, clients, frame.copy(), args.quality_large)

                time.sleep(1/30)
        finally:
            cap.release()
            if ser:
                ser.close()
            with clients_lock:
                for client in clients:
                    client.close()
            executor.shutdown(wait=False)
            print("[Sender] 已退出")

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Video stream sender server with timing")
    parser.add_argument('--ip', default='0.0.0.0', help='Server IP address')
    parser.add_argument('--port', type=int, default=5001, help='Server port')
    parser.add_argument('--serial', default=None, help='Serial port (e.g., /dev/ttyS1)')
    parser.add_argument('--baudrate', type=int, default=115200, help='Serial baudrate')
    parser.add_argument('--camera', type=int, default=0, help='Camera index')
    parser.add_argument('--width', type=int, default=1280, help='Main stream width')
    parser.add_argument('--height', type=int, default=720, help='Main stream height')
    parser.add_argument('--small-width', type=int, default=640, help='Small stream width')
    parser.add_argument('--small-height', type=int, default=480, help='Small stream height')
    parser.add_argument('--quality-small', type=int, default=35, help='JPEG quality for small stream (1-100)')
    parser.add_argument('--quality-large', type=int, default=100, help='JPEG quality for snapshot (1-100)')
    args = parser.parse_args()
    run_sender_server(args)
