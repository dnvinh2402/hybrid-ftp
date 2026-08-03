#ifndef RDT_PACKET_H
#define RDT_PACKET_H

#include <cstdint>
#include <cstddef>

#pragma pack(push, 1) // Tránh alignment byte để tương thích mạng
struct RDTHeader {
    uint32_t seq_num;     // Số thứ tự gói tin
    uint32_t ack_num;     // Số thứ tự xác nhận ACK
    uint16_t checksum;    // Checksum kiểm tra lỗi dữ liệu
    uint16_t payload_len; // Kích thước phần dữ liệu thực sự
    uint8_t  flags;       // Flag: Bit 0=SYN, Bit 1=ACK, Bit 2=FIN
};
#pragma pack(pop)

constexpr size_t MAX_PAYLOAD_SIZE = 1400; // Để kích thước gói UDP < 1500 bytes (MTU)

struct RDTPacket {
    RDTHeader header;
    char payload[MAX_PAYLOAD_SIZE];
};

#endif 