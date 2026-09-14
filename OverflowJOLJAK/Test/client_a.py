import struct

# PACKET_HEADER: unsigned short(2) + unsigned char(1), pack(1)이라 패딩 없음
# cs_packet_dig / cs_packet_build: 헤더(3) + float x,y,z(12) = 15바이트

PKT_C2S_DIG = 13
PKT_C2S_BUILD = 14
PKT_S2C_DIG = 15
PKT_S2C_BUILD = 16

def make_dig_packet(x, y, z):
    size = 15  # 3(헤더) + 12(xyz)
    return struct.pack('<HBfff', size, PKT_C2S_DIG, x, y, z)

def make_build_packet(x, y, z):
    size = 15
    return struct.pack('<HBfff', size, PKT_C2S_BUILD, x, y, z)

import socket
import struct
import time

HOST = '127.0.0.1'
PORT = 7777

PKT_C2S_DIG = 13
PKT_C2S_BUILD = 14

def make_dig_packet(x, y, z):
    size = 15  # 3(헤더) + 12(xyz)
    return struct.pack('<HBfff', size, PKT_C2S_DIG, x, y, z)

def make_build_packet(x, y, z):
    size = 15
    return struct.pack('<HBfff', size, PKT_C2S_BUILD, x, y, z)

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((HOST, PORT))
    print(f'[A] 서버에 연결됨 ({HOST}:{PORT})')

    time.sleep(0.5)  # 접속 직후 서버가 보내는 초기 패킷들과 안 섞이게 살짝 대기

    pkt = make_build_packet(300.0, 400.0, 60.0)
    sock.sendall(pkt)
    print('[A] build 패킷 전송함: x=300.0 y=400.0 z=60.0')

    time.sleep(1)  # 서버가 처리하고 B에게 브로드캐스트할 시간 벌어주기
    sock.close()
    print('[A] 연결 종료')

if __name__ == '__main__':
    main()