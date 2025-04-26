import sys
import socket
import numpy as np
from PyQt5.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout,
                             QHBoxLayout, QLabel, QLineEdit, QPushButton, QComboBox, QCheckBox)
from PyQt5.QtCore import Qt, QThread, pyqtSignal
import pyaudio
import pyqtgraph as pg
import time
import wave
from datetime import datetime
import logging
import threading

# Setup logging
logging.basicConfig(level=logging.DEBUG, format='%(asctime)s - %(levelname)s - %(message)s')

# Audio configuration
DEFAULT_SAMPLE_RATE = 44100
CHANNELS = 1
FORMAT = pyaudio.paInt16
FRAMES_PER_BUFFER = 1024
PLOT_SECONDS = 5
PLOT_DOWNSAMPLE = 10
SOCKET_TIMEOUT = 0.1

class StreamThread(QThread):
    data_received = pyqtSignal(np.ndarray)
    error_occurred = pyqtSignal(str)

    def __init__(self, host, port, sample_rate, play_audio=True):
        super().__init__()
        self.host = host
        self.port = port
        self.sample_rate = sample_rate
        self.play_audio = play_audio
        self.running = True
        self.recording = False
        self.recorded_samples = []
        self.p = None
        self.stream = None
        self.sock = None
        self.lock = threading.Lock()
        if sample_rate < 22050:
            logging.warning(f"Low sample rate ({sample_rate} Hz) may cause delays. Consider matching server rate (e.g., 44100 Hz).")
        self._init_audio()

    def _init_audio(self):
        with self.lock:
            try:
                self.p = pyaudio.PyAudio()
                if self.play_audio:
                    self.stream = self.p.open(format=FORMAT,
                                             channels=CHANNELS,
                                             rate=self.sample_rate,
                                             output=True,
                                             frames_per_buffer=FRAMES_PER_BUFFER)
                    logging.debug("PyAudio stream opened for playback")
            except Exception as e:
                logging.error(f"Failed to initialize PyAudio: {e}")
                self.error_occurred.emit(f"Audio initialization failed: {e}")

    def set_play_audio(self, play_audio):
        with self.lock:
            if play_audio != self.play_audio:
                self.play_audio = play_audio
                if self.play_audio and self.stream is None and self.p is not None:
                    try:
                        self.stream = self.p.open(format=FORMAT,
                                                 channels=CHANNELS,
                                                 rate=self.sample_rate,
                                                 output=True,
                                                 frames_per_buffer=FRAMES_PER_BUFFER)
                        logging.debug("PyAudio stream opened for playback")
                    except Exception as e:
                        logging.error(f"Failed to open PyAudio stream: {e}")
                        self.error_occurred.emit(f"Failed to enable audio: {e}")
                elif not self.play_audio and self.stream is not None:
                    try:
                        self.stream.stop_stream()
                        self.stream.close()
                        self.stream = None
                        logging.debug("PyAudio stream closed")
                    except Exception as e:
                        logging.error(f"Failed to close PyAudio stream: {e}")

    def set_sample_rate(self, sample_rate):
        with self.lock:
            if sample_rate != self.sample_rate:
                if sample_rate < 22050:
                    logging.warning(f"Low sample rate ({sample_rate} Hz) may cause delays. Consider matching server rate (e.g., 44100 Hz).")
                logging.debug(f"Changing sample rate to {sample_rate}")
                if self.recording:
                    self.stop_recording()
                    self.recording = True
                    self.recorded_samples = []
                if self.stream is not None:
                    try:
                        self.stream.stop_stream()
                        self.stream.close()
                        self.stream = None
                        logging.debug("PyAudio stream closed for sample rate change")
                    except Exception as e:
                        logging.error(f"Error closing stream for sample rate change: {e}")
                self.sample_rate = sample_rate
                if self.play_audio and self.p is not None:
                    try:
                        self.stream = self.p.open(format=FORMAT,
                                                 channels=CHANNELS,
                                                 rate=self.sample_rate,
                                                 output=True,
                                                 frames_per_buffer=FRAMES_PER_BUFFER)
                        logging.debug(f"PyAudio stream reopened with sample rate {sample_rate}")
                    except Exception as e:
                        logging.error(f"Failed to reopen PyAudio stream: {e}")
                        self.error_occurred.emit(f"Failed to update audio: {e}")

    def start_recording(self):
        with self.lock:
            self.recording = True
            self.recorded_samples = []
            logging.debug("Recording started")

    def stop_recording(self):
        with self.lock:
            self.recording = False
            if self.recorded_samples:
                timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
                filename = f"recording_{timestamp}.wav"
                try:
                    with wave.open(filename, 'wb') as wf:
                        wf.setnchannels(CHANNELS)
                        wf.setsampwidth(2)
                        wf.setframerate(self.sample_rate)
                        wf.writeframes(np.concatenate(self.recorded_samples).tobytes())
                    logging.info(f"Saved recording to {filename}")
                except Exception as e:
                    logging.error(f"Error saving recording: {e}")
            self.recorded_samples = []
            logging.debug("Recording stopped")

    def sync(self):
        with self.lock:
            logging.debug("Synchronizing stream")
            # Flush socket buffer
            if self.sock is not None:
                try:
                    self.sock.setblocking(False)
                    while True:
                        try:
                            self.sock.recv(FRAMES_PER_BUFFER * 2)
                        except BlockingIOError:
                            break
                    self.sock.settimeout(SOCKET_TIMEOUT)
                    logging.debug("Socket buffer flushed")
                except Exception as e:
                    logging.error(f"Error flushing socket buffer: {e}")
            # Reset PyAudio stream
            if self.play_audio and self.stream is not None:
                try:
                    self.stream.stop_stream()
                    self.stream.close()
                    self.stream = self.p.open(format=FORMAT,
                                             channels=CHANNELS,
                                             rate=self.sample_rate,
                                             output=True,
                                             frames_per_buffer=FRAMES_PER_BUFFER)
                    logging.debug("PyAudio stream reset for sync")
                except Exception as e:
                    logging.error(f"Error resetting PyAudio stream: {e}")
                    self.error_occurred.emit(f"Failed to sync audio: {e}")
            # Clear recording buffer if active
            if self.recording:
                self.recorded_samples = []
                logging.debug("Recording buffer cleared for sync")

    def run(self):
        logging.debug("StreamThread started")
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(SOCKET_TIMEOUT)
            self.sock.connect((self.host, self.port))
            logging.debug(f"Connected to {self.host}:{self.port}")

            while self.running:
                try:
                    data = self.sock.recv(FRAMES_PER_BUFFER * 2)
                    if not data:
                        raise ConnectionError("Server disconnected")
                except socket.timeout:
                    if not self.running:
                        break
                    continue

                samples = np.frombuffer(data, dtype=np.int16)
                if len(samples) != FRAMES_PER_BUFFER:
                    continue

                with self.lock:
                    if self.play_audio and self.stream is not None and self.running:
                        try:
                            self.stream.write(data)
                        except Exception as e:
                            logging.error(f"Error writing to stream: {e}")
                    if self.recording:
                        self.recorded_samples.append(samples.copy())
                self.data_received.emit(samples)

            self.sock.close()
            self.sock = None
            logging.debug("Socket closed")
        except Exception as e:
            logging.error(f"StreamThread error: {e}")
            self.error_occurred.emit(str(e))
        finally:
            self.cleanup()

    def cleanup(self):
        logging.debug("StreamThread cleanup started")
        with self.lock:
            if self.sock is not None:
                try:
                    self.sock.close()
                    logging.debug("Socket closed in cleanup")
                except Exception as e:
                    logging.error(f"Error closing socket: {e}")
                self.sock = None
            if self.stream is not None:
                try:
                    self.stream.stop_stream()
                    self.stream.close()
                    logging.debug("PyAudio stream closed in cleanup")
                except Exception as e:
                    logging.error(f"Error closing stream in cleanup: {e}")
                self.stream = None
            if self.p is not None:
                try:
                    self.p.terminate()
                    logging.debug("PyAudio terminated")
                except Exception as e:
                    logging.error(f"Error terminating PyAudio: {e}")
                self.p = None
            if self.recording:
                self.stop_recording()
        logging.debug("StreamThread cleanup completed")

    def stop(self):
        with self.lock:
            self.running = False
            if self.sock is not None:
                try:
                    self.sock.close()
                    logging.debug("Socket closed in stop")
                except Exception as e:
                    logging.error(f"Error closing socket in stop: {e}")
                self.sock = None
        logging.debug("StreamThread stop requested")

class WaveformWidget(pg.PlotWidget):
    def __init__(self, parent=None, sample_rate=DEFAULT_SAMPLE_RATE):
        super().__init__(parent)
        self.setBackground('w')
        self.showGrid(x=True, y=True)
        self.setLabel('left', 'Amplitude')
        self.setLabel('bottom', 'Time', 's')
        self.setTitle('Audio Waveform')
        self.setYRange(-32768, 32768)
        self.setXRange(-PLOT_SECONDS, 0)

        self.sample_rate = sample_rate
        self.buffer_size = int(sample_rate * PLOT_SECONDS / PLOT_DOWNSAMPLE)
        self.samples = np.zeros(self.buffer_size, dtype=np.int16)
        self.times = np.linspace(-PLOT_SECONDS, 0, self.buffer_size)
        self.pos = 0

        self.plot_item = self.plot(self.times, self.samples, pen='b')
        logging.debug("WaveformWidget initialized")

    def update_plot(self, new_samples):
        downsampled = new_samples[::PLOT_DOWNSAMPLE]
        num_new = len(downsampled)

        if self.pos + num_new <= self.buffer_size:
            self.samples[self.pos:self.pos + num_new] = downsampled
        else:
            first_part = self.buffer_size - self.pos
            self.samples[self.pos:] = downsampled[:first_part]
            second_part = num_new - first_part
            self.samples[:second_part] = downsampled[first_part:]

        self.pos = (self.pos + num_new) % self.buffer_size

        display_samples = np.roll(self.samples, -self.pos)
        self.plot_item.setData(self.times, display_samples)

    def set_sample_rate(self, sample_rate):
        if sample_rate != self.sample_rate:
            self.sample_rate = sample_rate
            self.buffer_size = int(sample_rate * PLOT_SECONDS / PLOT_DOWNSAMPLE)
            self.samples = np.zeros(self.buffer_size, dtype=np.int16)
            self.times = np.linspace(-PLOT_SECONDS, 0, self.buffer_size)
            self.pos = 0
            self.plot_item.setData(self.times, self.samples)
            logging.debug(f"WaveformWidget sample rate updated to {sample_rate}")

    def sync(self):
        self.samples = np.zeros(self.buffer_size, dtype=np.int16)
        self.pos = 0
        self.plot_item.setData(self.times, self.samples)
        logging.debug("WaveformWidget synchronized")

class AudioClient(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Audio Stream Client")
        self.setGeometry(100, 100, 800, 600)

        main_widget = QWidget()
        self.setCentralWidget(main_widget)
        main_layout = QVBoxLayout(main_widget)

        input_layout = QHBoxLayout()
        self.host_input = QLineEdit("127.0.0.1")
        self.port_input = QLineEdit("40918")
        self.sample_rate_combo = QComboBox()
        sample_rates = ["8000", "16000", "22050", "44100", "48000", "96000"]
        self.sample_rate_combo.addItems(sample_rates)
        self.sample_rate_combo.setCurrentText("44100")
        self.play_audio_check = QCheckBox("Play Audio")
        self.play_audio_check.setChecked(True)
        self.connect_button = QPushButton("Connect")
        self.record_button = QPushButton("Record")
        self.sync_button = QPushButton("Sync")
        self.sync_button.setEnabled(False)
        input_layout.addWidget(QLabel("Host:"))
        input_layout.addWidget(self.host_input)
        input_layout.addWidget(QLabel("Port:"))
        input_layout.addWidget(self.port_input)
        input_layout.addWidget(QLabel("Sample Rate:"))
        input_layout.addWidget(self.sample_rate_combo)
        input_layout.addWidget(self.play_audio_check)
        input_layout.addWidget(self.connect_button)
        input_layout.addWidget(self.record_button)
        input_layout.addWidget(self.sync_button)
        main_layout.addLayout(input_layout)

        self.status_label = QLabel("Disconnected")
        self.status_label.setAlignment(Qt.AlignCenter)
        main_layout.addWidget(self.status_label)

        self.sample_rate = int(self.sample_rate_combo.currentText())
        self.waveform = WaveformWidget(self, self.sample_rate)
        main_layout.addWidget(self.waveform)

        self.stream_thread = None

        self.connect_button.clicked.connect(self.start_stream)
        self.record_button.clicked.connect(self.toggle_recording)
        self.sample_rate_combo.currentTextChanged.connect(self.update_sample_rate)
        self.play_audio_check.stateChanged.connect(self.update_play_audio)
        self.sync_button.clicked.connect(self.sync_stream)
        logging.debug("AudioClient initialized")

    def update_sample_rate(self, rate_text):
        new_sample_rate = int(rate_text)
        if new_sample_rate != self.sample_rate:
            self.sample_rate = new_sample_rate
            logging.debug(f"Updating sample rate to {self.sample_rate}")
            if self.waveform is not None:
                self.centralWidget().layout().removeWidget(self.waveform)
                self.waveform.deleteLater()
                self.waveform = None
            self.waveform = WaveformWidget(self, self.sample_rate)
            self.centralWidget().layout().addWidget(self.waveform)
            if self.stream_thread and self.stream_thread.isRunning():
                try:
                    self.stream_thread.set_sample_rate(self.sample_rate)
                    self.stream_thread.data_received.connect(self.waveform.update_plot)
                    logging.debug("StreamThread sample rate updated")
                except Exception as e:
                    logging.error(f"Error updating stream sample rate: {e}")
                    self.handle_error(f"Failed to update sample rate: {e}")

    def update_play_audio(self, state):
        play_audio = state == Qt.Checked
        if self.stream_thread and self.stream_thread.isRunning():
            self.stream_thread.set_play_audio(play_audio)
            logging.debug(f"Play audio set to {play_audio}")

    def sync_stream(self):
        if self.stream_thread and self.stream_thread.isRunning():
            try:
                self.stream_thread.sync()
                self.waveform.sync()
                self.status_label.setText("Stream synchronized")
                logging.debug("Stream synchronized")
            except Exception as e:
                logging.error(f"Error synchronizing stream: {e}")
                self.handle_error(f"Failed to sync stream: {e}")
        else:
            self.status_label.setText("Error: Not connected to server")
            logging.error("Cannot sync: not connected")

    def start_stream(self):
        logging.debug("start_stream called")
        if self.stream_thread and self.stream_thread.isRunning():
            logging.debug("Disconnecting existing stream")
            try:
                self.stream_thread.data_received.disconnect(self.waveform.update_plot)
                self.stream_thread.error_occurred.disconnect(self.handle_error)
                logging.debug("Signals disconnected")
                self.stream_thread.stop()
                if not self.stream_thread.wait(2000):
                    logging.error("StreamThread failed to stop within timeout")
                    self.stream_thread.terminate()
                self.stream_thread = None
                logging.debug("StreamThread stopped successfully")
            except Exception as e:
                logging.error(f"Error stopping stream: {e}")
                self.handle_error(f"Failed to stop stream: {e}")
                return
            self.connect_button.setText("Connect")
            self.status_label.setText("Disconnected")
            self.record_button.setEnabled(False)
            self.sync_button.setEnabled(False)
            self.connect_button.clicked.disconnect()
            self.connect_button.clicked.connect(self.start_stream)
            return

        host = self.host_input.text()
        try:
            port = int(self.port_input.text())
        except ValueError:
            self.status_label.setText("Error: Invalid port number")
            logging.error("Invalid port number")
            return

        logging.debug(f"Starting new stream: {host}:{port}, sample_rate={self.sample_rate}")
        self.stream_thread = StreamThread(host, port, self.sample_rate, self.play_audio_check.isChecked())
        self.stream_thread.data_received.connect(self.waveform.update_plot)
        self.stream_thread.error_occurred.connect(self.handle_error)
        self.stream_thread.start()

        self.connect_button.setText("Disconnect")
        self.status_label.setText("Connected")
        self.record_button.setEnabled(True)
        self.sync_button.setEnabled(True)
        self.connect_button.clicked.disconnect()
        self.connect_button.clicked.connect(self.start_stream)
        logging.debug("Stream started")

    def toggle_recording(self):
        if not self.stream_thread or not self.stream_thread.isRunning():
            self.status_label.setText("Error: Not connected to server")
            logging.error("Cannot record: not connected")
            return

        if self.stream_thread.recording:
            self.stream_thread.stop_recording()
            self.record_button.setText("Record")
            self.status_label.setText("Connected")
            logging.debug("Recording stopped via toggle")
        else:
            self.stream_thread.start_recording()
            self.record_button.setText("Stop Recording")
            self.status_label.setText("Recording")
            logging.debug("Recording started via toggle")

    def handle_error(self, error_msg):
        logging.error(f"Handling error: {error_msg}")
        if self.stream_thread:
            try:
                self.stream_thread.data_received.disconnect(self.waveform.update_plot)
                self.stream_thread.error_occurred.disconnect(self.handle_error)
                logging.debug("Signals disconnected in handle_error")
                self.stream_thread.stop()
                if not self.stream_thread.wait(2000):
                    logging.error("StreamThread failed to stop within timeout")
                    self.stream_thread.terminate()
                self.stream_thread = None
                logging.debug("StreamThread stopped due to error")
            except Exception as e:
                logging.error(f"Error stopping stream in handle_error: {e}")
        self.status_label.setText(f"Error: {error_msg}")
        self.connect_button.setText("Connect")
        self.record_button.setText("Record")
        self.record_button.setEnabled(False)
        self.sync_button.setEnabled(False)
        self.connect_button.clicked.disconnect()
        self.connect_button.clicked.connect(self.start_stream)

    def closeEvent(self, event):
        logging.debug("closeEvent triggered")
        if self.stream_thread:
            try:
                self.stream_thread.data_received.disconnect(self.waveform.update_plot)
                self.stream_thread.error_occurred.disconnect(self.handle_error)
                logging.debug("Signals disconnected in closeEvent")
                self.stream_thread.stop()
                if not self.stream_thread.wait(2000):
                    logging.error("StreamThread failed to stop within timeout")
                    self.stream_thread.terminate()
                self.stream_thread = None
                logging.debug("StreamThread stopped in closeEvent")
            except Exception as e:
                logging.error(f"Error stopping stream in closeEvent: {e}")
        event.accept()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    pg.setConfigOptions(antialias=True)
    window = AudioClient()
    window.show()
    sys.exit(app.exec_())
