import sys
import socket
import struct
import cv2
import numpy as np
from PyQt5.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                             QHBoxLayout, QLabel, QLineEdit, QPushButton)
from PyQt5.QtGui import QImage, QPixmap
from PyQt5.QtCore import Qt, QThread, pyqtSignal
from datetime import datetime
import io

class StreamThread(QThread):
    frame_received = pyqtSignal(np.ndarray)
    error_occurred = pyqtSignal(str)

    def __init__(self, host, port):
        super().__init__()
        self.host = host
        self.port = port
        self.running = True

    def run(self):
        try:
            # Connect to the server
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((self.host, self.port))

            while self.running:
                # Read frame size (4 bytes, network byte order)
                size_data = b""
                while len(size_data) < 4 and self.running:
                    chunk = sock.recv(4 - len(size_data))
                    if not chunk:
                        raise ConnectionError("Connection closed by server")
                    size_data += chunk
                if not self.running:
                    break

                size = struct.unpack("!I", size_data)[0]

                # Read frame data
                frame_data = b""
                while len(frame_data) < size and self.running:
                    chunk = sock.recv(size - len(frame_data))
                    if not chunk:
                        raise ConnectionError("Connection closed by server")
                    frame_data += chunk
                if not self.running:
                    break

                # Decode JPEG to image
                frame_array = np.frombuffer(frame_data, dtype=np.uint8)
                frame = cv2.imdecode(frame_array, cv2.IMREAD_COLOR)
                if frame is None:
                    continue

                # Convert BGR to RGB for Qt
                frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                self.frame_received.emit(frame_rgb)

            sock.close()
        except Exception as e:
            self.error_occurred.emit(str(e))

    def stop(self):
        self.running = False

class VideoClient(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Video Stream Client")
        self.setGeometry(100, 100, 800, 600)

        # Main widget and layout
        main_widget = QWidget()
        self.setCentralWidget(main_widget)
        main_layout = QVBoxLayout(main_widget)

        # Input fields for host and port
        input_layout = QHBoxLayout()
        self.host_input = QLineEdit("127.0.0.1")
        self.port_input = QLineEdit("40917")
        self.connect_button = QPushButton("Connect")
        input_layout.addWidget(QLabel("Host:"))
        input_layout.addWidget(self.host_input)
        input_layout.addWidget(QLabel("Port:"))
        input_layout.addWidget(self.port_input)
        input_layout.addWidget(self.connect_button)
        main_layout.addLayout(input_layout)

        # Video display
        self.video_label = QLabel()
        self.video_label.setAlignment(Qt.AlignCenter)
        main_layout.addWidget(self.video_label)

        # Save button
        self.save_button = QPushButton("Save Snapshot")
        self.save_button.setEnabled(False)  # Disabled until connected
        main_layout.addWidget(self.save_button)

        # Current frame
        self.current_frame = None

        # Stream thread
        self.stream_thread = None

        # Connect signals
        self.connect_button.clicked.connect(self.start_stream)
        self.save_button.clicked.connect(self.save_snapshot)

    def start_stream(self):
        if self.stream_thread and self.stream_thread.isRunning():
            self.stream_thread.stop()
            self.stream_thread.wait()

        host = self.host_input.text()
        try:
            port = int(self.port_input.text())
        except ValueError:
            self.video_label.setText("Error: Invalid port number")
            return

        self.stream_thread = StreamThread(host, port)
        self.stream_thread.frame_received.connect(self.update_frame)
        self.stream_thread.error_occurred.connect(self.handle_error)
        self.stream_thread.start()

        self.connect_button.setText("Disconnect")
        self.connect_button.clicked.disconnect()
        self.connect_button.clicked.connect(self.stop_stream)
        self.save_button.setEnabled(True)

    def stop_stream(self):
        if self.stream_thread:
            self.stream_thread.stop()
            self.stream_thread.wait()
            self.stream_thread = None

        self.video_label.clear()
        self.video_label.setText("Disconnected")
        self.connect_button.setText("Connect")
        self.connect_button.clicked.disconnect()
        self.connect_button.clicked.connect(self.start_stream)
        self.save_button.setEnabled(False)

    def update_frame(self, frame):
        self.current_frame = frame
        h, w, c = frame.shape
        image = QImage(frame.data, w, h, w * c, QImage.Format_RGB888)
        pixmap = QPixmap.fromImage(image)
        self.video_label.setPixmap(pixmap.scaled(self.video_label.size(), 
                                                Qt.KeepAspectRatio, 
                                                Qt.SmoothTransformation))

    def save_snapshot(self):
        if self.current_frame is None:
            return

        # Generate timestamped filename
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"snapshot_{timestamp}.jpg"

        # Convert back to BGR for saving
        frame_bgr = cv2.cvtColor(self.current_frame, cv2.COLOR_RGB2BGR)
        cv2.imwrite(filename, frame_bgr)
        print(f"Saved snapshot to {filename}")

    def handle_error(self, error_msg):
        self.stop_stream()
        self.video_label.setText(f"Error: {error_msg}")

    def closeEvent(self, event):
        if self.stream_thread:
            self.stream_thread.stop()
            self.stream_thread.wait()
        event.accept()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = VideoClient()
    window.show()
    sys.exit(app.exec_())
