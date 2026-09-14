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

HOST = '127.0.0.1'
PORT = 7777

PACKET_TYPE_NAMES = {
    1: 'PKT_C2S_PLAYER_MOVE',
    2: 'PKT_S2C_ADD_PLAYER',
    3: 'PKT_S2C_PLAYER_POSITION',
    4: 'PKT_S2C_REMOVE_PLAYER',
    5: 'PKT_S2C_MONSTER_SPAWN',
    6: 'PKT_S2C_MONSTER_POSITION',
    7: 'PKT_S2C_MONSTER_ATTACK',
    8: 'PKT_C2S_PLAYER_ATTACK',
    9: 'PKT_S2C_MONSTER_HP',
    10: 'PKT_S2C_MONSTER_REMOVE',
    11: 'PKT_C2S_FIRE',
    12: 'PKT_S2C_PLAYER_FIRE',
    13: 'PKT_C2S_DIG',
    14: 'PKT_C2S_BUILD',
    15: 'PKT_S2C_DIG',
    16: 'PKT_S2C_BUILD',
}

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((HOST, PORT))
    print(f'[B] 서버에 연결됨 ({HOST}:{PORT}) - 대기 중...\n')

    buf = b''
    while True:
        data = sock.recv(4096)
        if not data:
            print('[B] 서버가 연결을 끊음')
            break
        buf += data

        # 헤더(3바이트: unsigned short size + unsigned char type)만큼도 안 왔으면 더 받기
        while len(buf) >= 3:
            size, ptype = struct.unpack_from('<HB', buf, 0)
            if len(buf) < size:
                break  # 아직 패킷 전체가 안 옴

            body = buf[3:size]
            name = PACKET_TYPE_NAMES.get(ptype, f'UNKNOWN({ptype})')

            if ptype == 15 or ptype == 16:  # PKT_S2C_DIG / PKT_S2C_BUILD
                x, y, z = struct.unpack('<fff', body)
                print(f'[B] 받음: {name}  x={x:.1f} y={y:.1f} z={z:.1f}')
            else:
                print(f'[B] 받음: {name}  (size={size})')

            buf = buf[size:]  # 처리한 만큼 버퍼에서 제거

if __name__ == '__main__':
    main()