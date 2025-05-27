import sounddevice as sd
import asyncio
import socket
import logging
import signal
import numpy as np
import argparse
import atexit
import os
from contextlib import redirect_stderr

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='[%(asctime)s] %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S'
)
logger = logging.getLogger(__name__)

running = True
audio_stream = None
server_socket = None

def list_alsa_devices():
    logger.info("Listing ALSA capture devices:")
    try:
        with open(os.devnull, 'w') as devnull, redirect_stderr(devnull):
            devices = sd.query_devices()
            cards = {}
            for i, dev in enumerate(devices):
                if dev['max_input_channels'] > 0:
                    alsa_name = dev['name']
                    if 'hw:' in alsa_name:
                        card_index = alsa_name.split('hw:')[1].split(',')[0]
                        if card_index not in cards:
                            cards[card_index] = {'name': f"hw:{card_index}", 'devices': []}
                        cards[card_index]['devices'].append((i, dev, alsa_name))

            for card_index in sorted(cards.keys(), key=int):
                card = cards[card_index]
                logger.info(f"Card {card_index}: {card['name']}")
                for dev_index, dev, alsa_name in card['devices']:
                    logger.info(f"  Device {alsa_name}: {dev['name']}")
                    min_rate = int(dev['default_samplerate'])
                    max_rate = min_rate
                    for rate in [8000, 16000, 22050, 44100, 48000, 96000, 192000]:
                        try:
                            sd.check_input_settings(
                                device=dev_index, channels=1, dtype='int16', samplerate=rate
                            )
                            max_rate = max(max_rate, rate)
                            min_rate = min(min_rate, rate)
                        except:
                            pass
                    logger.info(f"    Supported sample rates: {min_rate} - {max_rate} Hz")
    except Exception as e:
        logger.error(f"Error listing devices: {str(e)}")

async def handle_client(reader, writer, audio, buffer_size, client_addr):
    logger.info(f"Client connected from {client_addr}")
    buffer = np.zeros(buffer_size // 2, dtype=np.int16)
    try:
        while running:
            data, _ = audio.read(buffer_size // 2)
            buffer[:] = data[:, 0].astype(np.int16)  # Ensure mono int16
            try:
                writer.write(buffer.tobytes())
                await writer.drain()
                logger.debug(f"Sent {len(buffer) * 2} bytes to {client_addr}")
            except (ConnectionError, asyncio.TimeoutError):
                logger.info(f"Client {client_addr} disconnected")
                break
    except Exception as e:
        logger.error(f"Error in client {client_addr}: {str(e)}")
    finally:
        writer.close()
        await writer.wait_closed()
        logger.info(f"Client {client_addr} connection closed")

async def main(args):
    global audio_stream, server_socket, running

    try:
        # Configure audio stream to match C++ (S16_LE, mono, 44100 Hz)
        audio_stream = sd.InputStream(
            samplerate=args.sample_rate,
            channels=1,
            dtype='int16',  # 16-bit signed little-endian
            blocksize=args.buffer_size // 2,
            device=args.device,
            latency='low',
            extra_settings=sd.RawInputStream(extra_settings={'format': 'S16_LE'})
        )
        audio_stream.start()
        logger.info("Audio stream started")

        server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server_socket.setblocking(False)
        server_socket.bind(('0.0.0.0', args.port))
        server_socket.listen(5)

        logger.info(f"Server listening on port {args.port}")
        logger.info(f"ALSA device: {args.device}")
        logger.info(f"ALSA sample rate: {args.sample_rate} Hz")
        logger.info(f"ALSA buffer size: {args.buffer_size} bytes")

        server = await asyncio.start_server(
            lambda r, w: handle_client(r, w, audio_stream, args.buffer_size,
                                      f"{w.get_extra_info('peername')[0]}"),
            sock=server_socket
        )

        def cleanup():
            global running
            running = False
            if audio_stream:
                audio_stream.stop()
                audio_stream.close()
            if server_socket:
                server_socket.close()
            logger.info("Server shutdown complete")

        atexit.register(cleanup)

        def handle_shutdown(loop):
            tasks = [task for task in asyncio.all_tasks(loop) if task is not asyncio.current_task()]
            for task in tasks:
                task.cancel()
            loop.stop()
            loop.run_until_complete(loop.shutdown_asyncgens())
            loop.close()

        loop = asyncio.get_running_loop()
        loop.add_signal_handler(signal.SIGINT, lambda: handle_shutdown(loop))

        async with server:
            await server.serve_forever()

    except Exception as e:
        logger.error(f"Error: {str(e)}")
        raise
    finally:
        cleanup()

def parse_args():
    parser = argparse.ArgumentParser(description='Low-latency audio streaming server')
    parser.add_argument('--port', type=int, default=40918, help='Server port')
    parser.add_argument('--sample-rate', type=int, default=44100, help='Sample rate in Hz')
    parser.add_argument('--buffer-size', type=int, default=2048, help='Buffer size in bytes')
    parser.add_argument('--device', type=str, default='hw:0,0', help='ALSA device name (e.g., hw:0,0)')
    parser.add_argument('--list-devices', '--list-device', action='store_true', help='List available ALSA devices')
    return parser.parse_args()

if __name__ == '__main__':
    args = parse_args()
    if args.list_devices:
        list_alsa_devices()
    else:
        try:
            asyncio.run(main(args))
        except asyncio.CancelledError:
            pass
