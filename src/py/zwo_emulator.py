#!/usr/bin/env python3
"""
ZWO Camera Server Emulator

This script emulates the ZWO camera server protocol, providing a simulated
camera that responds to the same commands as the real zwoserver.

Protocol Summary (from zwoserver.c):
- TCP socket connection on port 52311 (default)
- Text-based command/response protocol (newline terminated)
- Binary image data sent after "data" command

Commands:
- version: Returns server version, cookie, and startup time
- open: Opens camera, returns "width height cooler color bitDepth model"
- close: Closes camera
- setup [params]: Configures ROI (x, y, w, h, bin, bits)
- exptime [seconds]: Sets/gets exposure time
- gain [value]: Sets/gets gain
- offset [value]: Sets/gets offset
- status: Returns camera state (closed/idle/exposing/streaming)
- expose: Starts exposure
- data [max_size]: Gets image data after exposure
- tempcon [temp|off]: Temperature control
- filter [position]: Filter wheel control
- quit: Terminates server

Image Format:
- Raw pixel data (8-bit or 16-bit depending on configuration)
- Size: width * height * (bits/8) bytes
- 16-bit data is little-endian
"""

import socket
import threading
import struct
import time
import argparse
import numpy as np
from typing import Optional, Tuple


class ZwoEmulator:
    """Emulates a ZWO camera server."""
    
    # Default camera properties (simulating ASI294MM Pro)
    DEFAULT_WIDTH = 4656
    DEFAULT_HEIGHT = 3520
    DEFAULT_COOLER = 1
    DEFAULT_COLOR = 0
    DEFAULT_BIT_DEPTH = 12
    DEFAULT_MODEL = "ASI294MM_Pro"
    
    # Server states
    STATE_CLOSED = "closed"
    STATE_IDLE = "idle"
    STATE_EXPOSING = "exposing"
    STATE_VIDEO = "streaming"
    
    def __init__(self, port: int = 52311, random_seed: Optional[int] = None):
        self.port = port
        self.running = False
        self.server_socket = None
        self.lock = threading.Lock()
        
        # Random generator for reproducible images
        self.rng = np.random.default_rng(random_seed)
        
        # Camera state
        self.state = self.STATE_CLOSED
        self.startup_time = int(time.time())
        self.cookie = self.rng.integers(0, 2**32)
        self.offtime = 0  # Time offset for synchronization
        # Fixed set of serial numbers (pick one based on random seed)
        serial_numbers = ["02a1b3c4d5e6f789", "124494e37ecc280e", "98f7e6d5c4b3a210"]
        self.serial_number = serial_numbers[self.rng.integers(0, len(serial_numbers))]
        
        # Camera configuration
        self.width = self.DEFAULT_WIDTH
        self.height = self.DEFAULT_HEIGHT
        self.cooler = self.DEFAULT_COOLER
        self.color = self.DEFAULT_COLOR
        self.bit_depth = self.DEFAULT_BIT_DEPTH
        self.model = self.DEFAULT_MODEL
        
        # ROI settings
        self.roi_x = 0
        self.roi_y = 0
        self.roi_w = self.width
        self.roi_h = self.height
        self.binning = 1
        self.bits = 16  # 8 or 16
        
        # Exposure settings
        self.exp_time = 0.02  # seconds
        self.gain = 0
        self.offset = 10
        
        # Temperature control
        self.temperature = -10.0
        self.target_temp = -10.0
        self.cooler_power = 50.0
        self.fan_on = 1
        
        # Exposure tracking
        self.exposure_start_time = 0.0
        self.image_data: Optional[bytes] = None
        self.image_size = 0
        
        # Filter wheel
        self.filter_count = 7
        self.filter_position = 0
        
        # Video streaming
        self.video_seq = 0
        self.video_last = 0
        self.video_thread: Optional[threading.Thread] = None
        self.video_data: Optional[bytes] = None
        
        # Tracking star for video mode (simulates a guide star that drifts)
        self.star_x = 0.0
        self.star_y = 0.0
        self.star_center_x = 0.0  # Initial center position
        self.star_center_y = 0.0
        self.star_initialized = False
        
    def _draw_gaussian_star(self, image: np.ndarray, x: float, y: float, 
                            brightness: float, sigma: float = 2.5) -> None:
        """Draw a Gaussian star profile at the given position."""
        height, width = image.shape
        
        # Define the region to draw (5 sigma should be enough)
        radius = int(5 * sigma)
        x_int, y_int = int(round(x)), int(round(y))
        
        x_min = max(0, x_int - radius)
        x_max = min(width, x_int + radius + 1)
        y_min = max(0, y_int - radius)
        y_max = min(height, y_int + radius + 1)
        
        if x_max <= x_min or y_max <= y_min:
            return
            
        # Create coordinate grids for the region
        yy, xx = np.mgrid[y_min:y_max, x_min:x_max]
        
        # Calculate Gaussian profile (sub-pixel accurate)
        dist_sq = (xx - x) ** 2 + (yy - y) ** 2
        gaussian = brightness * np.exp(-dist_sq / (2 * sigma ** 2))
        
        # Add to image (saturate at max value)
        max_val = np.iinfo(image.dtype).max if image.dtype != np.float64 else 65535
        region = image[y_min:y_max, x_min:x_max].astype(np.float64)
        region += gaussian
        np.clip(region, 0, max_val, out=region)
        image[y_min:y_max, x_min:x_max] = region.astype(image.dtype)
        
    def generate_random_image(self, is_video: bool = False) -> bytes:
        """Generate a random image with realistic noise characteristics."""
        size = self.roi_w * self.roi_h
        
        if self.bits == 8:
            # 8-bit image with noise
            base_level = 30 + self.offset
            noise = self.rng.poisson(base_level, size).astype(np.uint8)
            image = noise.reshape((self.roi_h, self.roi_w))
            
            # Add tracking star for video mode
            if is_video:
                if not self.star_initialized:
                    # Initialize star near center
                    self.star_center_x = self.roi_w / 2
                    self.star_center_y = self.roi_h / 2
                    self.star_x = self.star_center_x + self.rng.uniform(-5, 5)
                    self.star_y = self.star_center_y + self.rng.uniform(-5, 5)
                    self.star_initialized = True
                else:
                    # Move star randomly by a few pixels (simulates seeing/drift)
                    self.star_x += self.rng.uniform(-2.0, 2.0)
                    self.star_y += self.rng.uniform(-2.0, 2.0)
                    # Keep star within 10 pixels of center
                    self.star_x = np.clip(self.star_x, self.star_center_x - 10, self.star_center_x + 10)
                    self.star_y = np.clip(self.star_y, self.star_center_y - 10, self.star_center_y + 10)
                    # Also keep star in frame
                    self.star_x = np.clip(self.star_x, 10, self.roi_w - 10)
                    self.star_y = np.clip(self.star_y, 10, self.roi_h - 10)
                
                # Draw Gaussian star
                self._draw_gaussian_star(image, self.star_x, self.star_y, 
                                        brightness=180, sigma=2.0)
            else:
                # Add some random stars for still images
                num_stars = int(size * 0.0001)
                star_positions = self.rng.integers(0, size, num_stars)
                star_brightness = self.rng.integers(100, 255, num_stars).astype(np.uint8)
                image.flat[star_positions] = star_brightness
                
            return image.tobytes()
        else:
            # 16-bit image with noise
            max_val = (1 << self.bit_depth) - 1
            base_level = int(200 + self.offset * 10)
            # Poisson noise for realistic CCD behavior
            noise = self.rng.poisson(base_level, size).astype(np.uint16)
            # Add read noise
            read_noise = self.rng.normal(0, 5, size).astype(np.int16)
            image = np.clip(noise.astype(np.int32) + read_noise, 0, max_val).astype(np.uint16)
            image = image.reshape((self.roi_h, self.roi_w))
            
            # Add tracking star for video mode
            if is_video:
                if not self.star_initialized:
                    # Initialize star near center
                    self.star_center_x = self.roi_w / 2
                    self.star_center_y = self.roi_h / 2
                    self.star_x = self.star_center_x + self.rng.uniform(-5, 5)
                    self.star_y = self.star_center_y + self.rng.uniform(-5, 5)
                    self.star_initialized = True
                else:
                    # Move star randomly by a few pixels (simulates seeing/drift)
                    self.star_x += self.rng.uniform(-2.0, 2.0)
                    self.star_y += self.rng.uniform(-2.0, 2.0)
                    # Keep star within 10 pixels of center
                    self.star_x = np.clip(self.star_x, self.star_center_x - 10, self.star_center_x + 10)
                    self.star_y = np.clip(self.star_y, self.star_center_y - 10, self.star_center_y + 10)
                    # Also keep star in frame
                    self.star_x = np.clip(self.star_x, 10, self.roi_w - 10)
                    self.star_y = np.clip(self.star_y, 10, self.roi_h - 10)
                
                # Draw Gaussian star (brighter for 16-bit)
                self._draw_gaussian_star(image, self.star_x, self.star_y, 
                                        brightness=max_val * 0.7, sigma=2.5)
            else:
                # Add some random stars for still images
                num_stars = int(size * 0.0001)
                star_positions = self.rng.integers(0, size, num_stars)
                star_brightness = self.rng.integers(int(max_val * 0.3), max_val, num_stars).astype(np.uint16)
                image.flat[star_positions] = star_brightness
                
            # Little-endian 16-bit
            return image.tobytes()
    
    def _video_thread_func(self):
        """Video capture thread - continuously generates frames."""
        while self.state == self.STATE_VIDEO:
            # Simulate frame capture time based on exposure time
            time.sleep(self.exp_time)
            
            # Generate new frame
            with self.lock:
                if self.state == self.STATE_VIDEO:
                    self.video_data = self.generate_random_image(is_video=True)
                    self.video_seq += 1
    
    def handle_command(self, command: str) -> Tuple[str, Optional[bytes]]:
        """
        Handle a command and return (response_text, optional_binary_data).
        """
        parts = command.strip().split()
        if not parts:
            return "-Einvalid command", None
            
        cmd = parts[0].lower()
        args = parts[1:] if len(parts) > 1 else []
        
        binary_data = None
        
        with self.lock:
            if cmd == "version":
                response = f"1.0.4 {self.cookie} {self.startup_time}"
                
            elif cmd == "offtime":
                # Set/get time offset for synchronization
                if args:
                    self.offtime = int(time.time()) - int(args[0])
                response = str(self.offtime)
                
            elif cmd == "asigetnum":
                # Number of connected cameras
                response = "1"
                
            elif cmd == "asigetserialnumber":
                # Return camera serial number as hex string
                response = self.serial_number
                
            elif cmd == "open":
                if self.state == self.STATE_CLOSED:
                    self.state = self.STATE_IDLE
                response = f"{self.width} {self.height} {self.cooler} {self.color} {self.bit_depth} {self.model}"
                
            elif cmd == "close":
                if self.state != self.STATE_CLOSED:
                    self.state = self.STATE_CLOSED
                response = "OK"
                
            elif cmd == "setup":
                if self.state != self.STATE_IDLE:
                    return "-Eerr=22", None  # E_not_idle
                    
                if args:
                    if args[0].lower().startswith("def"):
                        # defaults
                        pass
                    elif args[0].lower().startswith("image"):
                        self.roi_x = 0
                        self.roi_y = 0
                        self.binning = int(args[1]) if len(args) > 1 else 1
                        self.roi_w = self.width // self.binning
                        self.roi_h = self.height // self.binning
                        self.bits = 16
                    elif args[0].lower().startswith("video"):
                        self.roi_x = 0
                        self.roi_y = 0
                        self.binning = int(args[1]) if len(args) > 1 else 1
                        self.roi_w = self.width // self.binning
                        self.roi_h = self.height // self.binning
                        self.bits = 8
                    elif len(args) >= 6:
                        self.roi_x = int(args[0])
                        self.roi_y = int(args[1])
                        self.roi_w = int(args[2])
                        self.roi_h = int(args[3])
                        self.binning = int(args[4])
                        self.bits = int(args[5])
                        # Align dimensions
                        self.roi_w = (self.roi_w // 8) * 8
                        self.roi_h = (self.roi_h // 2) * 2
                        
                response = f"{self.roi_x} {self.roi_y} {self.roi_w} {self.roi_h} {self.binning} {self.bits}"
                
            elif cmd == "exptime":
                if self.state == self.STATE_CLOSED:
                    return "-Eerr=21", None  # E_not_open
                if args:
                    self.exp_time = float(args[0])
                response = f"{self.exp_time:.6f}"
                
            elif cmd == "gain":
                if self.state == self.STATE_CLOSED:
                    return "-Eerr=21", None
                if args:
                    self.gain = int(args[0])
                response = str(self.gain)
                
            elif cmd == "offset":
                if self.state == self.STATE_CLOSED:
                    return "-Eerr=21", None
                if args:
                    self.offset = int(args[0])
                response = str(self.offset)
                
            elif cmd == "status":
                if self.state == self.STATE_EXPOSING:
                    elapsed = time.time() - self.exposure_start_time
                    if elapsed >= self.exp_time:
                        self.state = self.STATE_IDLE
                        response = "idle"
                    else:
                        response = f"exposing {elapsed:.1f}"
                else:
                    response = self.state
                    
            elif cmd == "expose":
                if self.state != self.STATE_IDLE:
                    return "-Eerr=22", None  # E_not_idle
                self.exposure_start_time = time.time()
                self.state = self.STATE_EXPOSING
                response = "OK"
                
            elif cmd == "data":
                # Check if exposure is complete
                if self.state == self.STATE_EXPOSING:
                    elapsed = time.time() - self.exposure_start_time
                    if elapsed >= self.exp_time:
                        self.state = self.STATE_IDLE
                
                # In video mode, data command also works (gets latest frame)
                if self.state == self.STATE_VIDEO:
                    if self.video_data:
                        self.image_data = self.video_data
                        self.image_size = len(self.image_data)
                    else:
                        return "-Eerr=23", None  # E_no_data
                elif self.state != self.STATE_IDLE:
                    return "-Eerr=22", None
                else:
                    # Generate image for still capture
                    self.image_data = self.generate_random_image()
                    self.image_size = len(self.image_data)
                
                # Optionally limit size
                max_size = int(args[0]) if args else self.image_size
                if max_size > 0:
                    binary_data = self.image_data[:max_size]
                    response = str(len(binary_data))
                else:
                    response = str(self.image_size)
                    
            elif cmd == "tempcon":
                if args:
                    if args[0].lower() == "off":
                        self.cooler_power = 0
                    else:
                        self.target_temp = float(args[0])
                        # Simulate cooling
                        if self.temperature > self.target_temp:
                            self.temperature = max(self.target_temp, self.temperature - 0.5)
                            self.cooler_power = min(100, 50 + (self.temperature - self.target_temp) * 5)
                response = f"{self.temperature:.1f} {self.cooler_power:.0f}"
                
            elif cmd == "fancon":
                if args:
                    if args[0].lower() == "on":
                        self.fan_on = 1
                    elif args[0].lower() == "off":
                        self.fan_on = 0
                response = str(self.fan_on)
                
            elif cmd == "filters":
                response = str(self.filter_count)
                
            elif cmd == "filter":
                if args:
                    self.filter_position = int(args[0])
                response = str(self.filter_position)
                
            elif cmd == "start":
                # Start video capture mode
                if self.state != self.STATE_IDLE:
                    return "-Eerr=22", None  # E_not_idle
                self.state = self.STATE_VIDEO
                self.video_seq = 0
                self.video_last = 0
                self.video_data = None
                self.star_initialized = False  # Reset star position for new video session
                # Start video thread
                self.video_thread = threading.Thread(target=self._video_thread_func)
                self.video_thread.daemon = True
                self.video_thread.start()
                response = "OK"
                
            elif cmd == "stop":
                # Stop video capture mode
                if self.state == self.STATE_VIDEO:
                    self.state = self.STATE_IDLE
                    # Thread will exit on next iteration
                    if self.video_thread:
                        self.video_thread.join(timeout=1.0)
                        self.video_thread = None
                response = "OK"
                
            elif cmd == "next":
                # Get next video frame (used in video streaming mode)
                # Format: next [timeout]
                # Returns: "sequence_num temperature cooler_power" + binary image data
                # Or "-Enodata" if no new frame within timeout
                if self.state != self.STATE_VIDEO:
                    return "-Eerr=24", None  # E_not_video
                    
                timeout = float(args[0]) if args else 0.0
                start_time = time.time()
                current_last = self.video_last
                
                # Release lock while waiting for new frame
                self.lock.release()
                try:
                    # Wait for new frame (video_seq > video_last)
                    while self.video_seq <= current_last:
                        if time.time() - start_time >= timeout:
                            break
                        time.sleep(0.005)
                finally:
                    self.lock.acquire()
                    
                if self.video_seq > current_last:
                    self.video_last = self.video_seq
                    # Copy current video data
                    self.image_data = self.video_data
                    self.image_size = len(self.image_data) if self.image_data else 0
                    binary_data = self.image_data
                    response = f"{self.video_last} {self.temperature:.1f} {self.cooler_power:.0f} {time.time_ns()}"
                else:
                    response = "-Enodata"
                
            elif cmd == "quit":
                self.running = False
                response = "OK"
                
            else:
                response = "-Eunknown command"
                
        return response, binary_data
    
    def handle_client(self, client_socket: socket.socket, address: tuple):
        """Handle a single client connection."""
        print(f"Connection accepted from {address[0]}:{address[1]}")
        
        try:
            buffer = b""
            while self.running:
                data = client_socket.recv(1024)
                if not data:
                    break
                    
                buffer += data
                
                # Process complete commands (newline-terminated)
                while b"\n" in buffer or b"\r" in buffer:
                    # Find the first terminator
                    nl_pos = buffer.find(b"\n")
                    cr_pos = buffer.find(b"\r")
                    
                    if nl_pos >= 0 and cr_pos >= 0:
                        term_pos = min(nl_pos, cr_pos)
                    elif nl_pos >= 0:
                        term_pos = nl_pos
                    else:
                        term_pos = cr_pos
                        
                    command = buffer[:term_pos].decode("utf-8", errors="ignore")
                    buffer = buffer[term_pos + 1:]
                    
                    if command:
                        response, binary_data = self.handle_command(command)
                        
                        # Send text response
                        client_socket.sendall((response + "\n").encode("utf-8"))
                        
                        # Send binary data if present
                        if binary_data:
                            client_socket.sendall(binary_data)
                            
        except ConnectionResetError:
            pass
        except Exception as e:
            print(f"Error handling client: {e}")
        finally:
            print(f"Connection closed from {address[0]}:{address[1]}")
            client_socket.close()
            # Reset state on disconnect
            with self.lock:
                self.state = self.STATE_CLOSED
    
    def start(self, blocking: bool = True):
        """Start the emulator server."""
        self.running = True
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind(("0.0.0.0", self.port))
        self.server_socket.listen(5)
        self.server_socket.settimeout(1.0)  # Allow periodic checks
        
        print(f"ZWO Emulator listening on port {self.port}")
        
        def serve():
            while self.running:
                try:
                    client_socket, address = self.server_socket.accept()
                    # Handle client in a new thread
                    client_thread = threading.Thread(
                        target=self.handle_client,
                        args=(client_socket, address)
                    )
                    client_thread.daemon = True
                    client_thread.start()
                except socket.timeout:
                    continue
                except OSError:
                    break
                    
        if blocking:
            serve()
        else:
            server_thread = threading.Thread(target=serve)
            server_thread.daemon = True
            server_thread.start()
            return server_thread
    
    def stop(self):
        """Stop the emulator server."""
        self.running = False
        if self.server_socket:
            self.server_socket.close()


def display_image(data: bytes, width: int, height: int, bits: int):
    """Display an image using matplotlib."""
    try:
        import matplotlib.pyplot as plt
        
        if bits == 8:
            image = np.frombuffer(data, dtype=np.uint8).reshape((height, width))
        else:
            image = np.frombuffer(data, dtype=np.uint16).reshape((height, width))
            
        plt.figure(figsize=(10, 8))
        plt.imshow(image, cmap='gray', origin='upper')
        plt.colorbar(label='ADU')
        plt.title(f'Simulated ZWO Image ({width}x{height}, {bits}-bit)')
        plt.xlabel('X (pixels)')
        plt.ylabel('Y (pixels)')
        plt.tight_layout()
        plt.show()
        
    except ImportError:
        print("matplotlib not available for image display")


def demo_client(port: int = 52311):
    """Demo client that connects and displays a random image."""
    import socket
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(("localhost", port))
    
    def send_cmd(cmd: str) -> str:
        sock.sendall((cmd + "\n").encode())
        return sock.recv(1024).decode().strip()
    
    try:
        # Open camera
        print(f"Version: {send_cmd('version')}")
        print(f"Open: {send_cmd('open')}")
        
        # Setup for small image (faster)
        print(f"Setup: {send_cmd('setup 0 0 512 512 1 16')}")
        
        # Set exposure
        print(f"Exptime: {send_cmd('exptime 0.1')}")
        
        # Expose
        print(f"Expose: {send_cmd('expose')}")
        
        # Wait for exposure
        time.sleep(0.15)
        print(f"Status: {send_cmd('status')}")
        
        # Get data
        response = send_cmd('data')
        print(f"Data: {response}")
        
        size = int(response)
        data = b""
        while len(data) < size:
            chunk = sock.recv(min(4096, size - len(data)))
            if not chunk:
                break
            data += chunk
            
        print(f"Received {len(data)} bytes")
        
        # Display
        display_image(data, 512, 512, 16)
        
        # Close
        print(f"Close: {send_cmd('close')}")
        
    finally:
        sock.close()


def main():
    parser = argparse.ArgumentParser(description="ZWO Camera Server Emulator")
    parser.add_argument("-p", "--port", type=int, default=52311,
                        help="Port to listen on (default: 52311)")
    parser.add_argument("-s", "--seed", type=int, default=None,
                        help="Random seed for reproducible images")
    parser.add_argument("--demo", action="store_true",
                        help="Run demo client instead of server")
    parser.add_argument("--demo-port", type=int, default=52311,
                        help="Port to connect demo client to")
    
    args = parser.parse_args()
    
    if args.demo:
        demo_client(args.demo_port)
    else:
        emulator = ZwoEmulator(port=args.port, random_seed=args.seed)
        try:
            emulator.start(blocking=True)
        except KeyboardInterrupt:
            print("\nShutting down...")
            emulator.stop()


if __name__ == "__main__":
    main()
