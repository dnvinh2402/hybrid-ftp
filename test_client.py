import socket
import time

def run_test():
    # Kết nối tới Server TCP port 2121
    s = socket.create_connection(('127.0.0.1', 2121))
    
    # 1. Nhận câu chào 220 từ Server
    print("Server:", s.recv(1024).decode().strip())

    # Danh sách lệnh test tuần tự
    commands = [
        "USER vinh",
        "PASS 123",
        "PWD",
        "NOOP",
        "QUIT"
    ]

    for cmd in commands:
        time.sleep(0.2) # Nghỉ nhẹ 0.2s giữa các lệnh
        print(f"\nClient -> {cmd}")
        
        # Gửi lệnh kèm \r\n
        s.sendall(f"{cmd}\r\n".encode())
        
        # Nhận phản hồi từ Server
        response = s.recv(1024).decode().strip()
        print(f"Server -> {response}")

    s.close()

if __name__ == "__main__":
    run_test()