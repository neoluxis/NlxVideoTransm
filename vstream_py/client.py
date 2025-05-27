import socket
import struct
import os
import threading
import queue
import numpy as np
import cv2
import argparse
import time
from concurrent.futures import ThreadPoolExecutor

# ---------- 默认配置 ----------
DEFAULT_SAVE_DIR = 'received_data/image'
BUFFER_SIZE = 4096
MAX_IMAGE_SIZE = 10 * 1024 * 1024

# ---------- 线程与队列 ----------
frame_queue = queue.Queue(maxsize=100)
executor = ThreadPoolExecutor(max_workers=2)

# ---------- 全局状态 ----------
image_counter = 1

def save_snapshot(data, idx, save_dir):
    """后台保存快照"""
    try:
        if len(data) > MAX_IMAGE_SIZE:
            print(f"[Client] 快照过大 ({len(data)} bytes), 丢弃")
            return
        decode_start = time.time()
        arr = np.frombuffer(data, np.uint8)
        img = cv2.imdecode(arr, cv2.IMREAD_COLOR)
        decode_time = time.time() - decode_start

        if img is None:
            print("[Client] 快照解码失败")
            return
        path = os.path.join(save_dir, f"snapshot_{idx}.jpg")
        if not cv2.imwrite(path, img, [int(cv2.IMWRITE_JPEG_QUALITY), 95]):
            print(f"[Client] 快照保存失败: {path}")
            return
        print(f"[Client] 快照保存: {path} | 解码耗时: {decode_time:.3f}s")
    except Exception as e:
        print(f"[Client] 快照保存异常: {e}")

from datetime import datetime

video_writer = None
video_start_time = None

def display_worker():
    global video_writer, video_start_time

    while True:
        frame = frame_queue.get()
        if frame is None:
            break
        try:
            if video_writer is None:
                h, w = frame.shape[:2]
                fourcc = cv2.VideoWriter_fourcc(*'XVID')  # or 'MJPG'
                video_start_time = datetime.now().strftime("%Y%m%d_%H%M%S")
                os.makedirs("videos", exist_ok=True)
                video_writer = cv2.VideoWriter(
                    f"videos/video_{video_start_time}.avi", fourcc, 25.0, (w, h)
                )
                print(f"[Client] 视频写入开始: video_{video_start_time}.avi")

            video_writer.write(frame)

            cv2.imshow('Viewer Stream', frame)
            key = cv2.waitKey(1)
            if key == 27:  # ESC 退出
                print("[Client] ESC 退出")
                break
        except Exception as e:
            print(f"[Client] 显示异常: {e}")
            break

    if video_writer:
        video_writer.release()
        print("[Client] 视频写入结束")
    cv2.destroyAllWindows()

i = 0
# def display_worker():
#     """显示线程"""
#     while True:
#         frame = frame_queue.get()
#         if frame is None:
#             break
#         try:
#             disp_start = time.time()
#             cv2.imshow('Viewer Stream', frame)
#             key = cv2.waitKey(1)
#             disp_time = time.time() - disp_start
#             if key == 27:
#                 print(f"[Client] ESC 退出 | 显示耗时: {disp_time:.3f}s")
#                 break
#             elif key == ord('s'):
#                 cv2.imwrite(f"images/{i:05d}.png", frame)
#                 i += 1
#         except Exception as e:
#             print(f"[Client] 显示异常: {e}")
#             break
#     cv2.destroyAllWindows()

def parse_messages(buf, save_dir):
    global image_counter
    offset = 0
    n = len(buf)
    while n - offset >= 4:
        L = struct.unpack('>L', buf[offset:offset + 4])[0]
        if n - offset < 4 + L:
            break
        offset += 4
        chunk = buf[offset:offset + L]
        offset += L

        try:
            decode_start = time.time()
            arr = np.frombuffer(chunk, np.uint8)
            frame = cv2.imdecode(arr, cv2.IMREAD_COLOR)
            decode_time = time.time() - decode_start

            if frame is None:
                print("[Client] 帧解码失败")
                continue

            if False and frame.shape[:2] == (720, 1280):  # 快照
                executor.submit(save_snapshot, chunk, image_counter, save_dir)
                image_counter += 1
            else:
                try:
                    frame_queue.put_nowait(frame)
                    print(f"[Client] 小流解码成功 | 解码耗时: {decode_time:.3f}s")
                except queue.Full:
                    print("[Client] 显示队列已满，丢弃帧")
        except Exception as e:
            print(f"[Client] 帧处理异常: {e}")
    return buf[offset:]

def run_viewer_client(args):
    os.makedirs(args.save_dir, exist_ok=True)

    display_thread = threading.Thread(target=display_worker, daemon=True)
    display_thread.start()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(5)
        try:
            s.connect((args.server_ip, args.port))
            s.settimeout(None)
            print(f"[Client] 已连接 {args.server_ip}:{args.port}")
            buf = b''
            total_recv = 0
            recv_start = time.time()

            while True:
                try:
                    data = s.recv(BUFFER_SIZE)
                    if not data:
                        print("[Client] 服务器断开")
                        break
                    recv_time = time.time() - recv_start
                    total_recv += len(data)
                    buf += data
                    buf = parse_messages(buf, args.save_dir)
                    recv_start = time.time()
                    print(f"[Client] 接收数据: {len(data)} bytes | 接收耗时: {recv_time:.3f}s")
                except Exception as e:
                    print(f"[Client] 接收异常: {e}")
                    break

        except socket.timeout:
            print("[Client] 连接超时")
        except ConnectionRefusedError:
            print("[Client] 服务器拒绝连接")
        except Exception as e:
            print(f"[Client] 连接异常: {e}")
        finally:
            frame_queue.put(None)
            display_thread.join(timeout=1)
            executor.shutdown(wait=False)
            print("[Client] 已退出")

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Video stream viewer client with timing")
    #parser.add_argument('--server-ip', default='192.168.144.163', help='Sender server IP')
    parser.add_argument('--server-ip', default='192.168.8.9', help='Sender server IP')
    parser.add_argument('--port', type=int, default=40917, help='Sender server port')
    parser.add_argument('--save-dir', default=DEFAULT_SAVE_DIR, help='Directory to save snapshots')
    args = parser.parse_args()
    run_viewer_client(args)
