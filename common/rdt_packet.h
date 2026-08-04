#ifndef RDT_PACKET_H
#define RDT_PACKET_H

#include <cstdint>
#include <cstddef>
#include <cstring>

#pragma pack(push, 1) // Tránh alignment byte để tương thích mạng
struct RDTHeader {
    uint32_t seq_num;     // Số thứ tự gói tin
    uint32_t ack_num;     // Số thứ tự xác nhận ACK
    uint16_t checksum;    // Checksum kiểm tra lỗi dữ liệu
    uint16_t payload_len; // Kích thước phần dữ liệu thực sự
    uint8_t  flags;       // Flag: Bit 0=SYN, Bit 1=ACK, Bit 2=FIN
};
#pragma pack(pop)


constexpr size_t MAX_PAYLOAD_SIZE = 1400; // Để kích thước gói UDP < 1500 byte
constexpr size_t RDT_HEADER_SIZE  = sizeof(RDTHeader);
 
struct RDTPacket {
    RDTHeader header;
    char payload[MAX_PAYLOAD_SIZE];
};
 
// Cac gia tri flag dung cho header.flags (dung namespace de tranh xung dot ten)
namespace RDTFlag {
    constexpr uint8_t SYN  = 1 << 0;
    constexpr uint8_t ACK  = 1 << 1;
    constexpr uint8_t DATA = 1 << 2;
    constexpr uint8_t FIN  = 1 << 3;
    constexpr uint8_t ABOR = 1 << 4;
}
 
// Internet checksum don gian (giong TCP/UDP): cong don 16-bit word,
// cong tran (carry) roi dao bit (one's complement).
inline uint16_t computeChecksum(const uint8_t* data, size_t len) {
    uint32_t sum = 0;
    size_t i = 0;
    for (; i + 1 < len; i += 2) {
        uint16_t word = (data[i] << 8) | data[i + 1];
        sum += word;
    }
    if (i < len) sum += (data[i] << 8); // byte le cuoi cung
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return static_cast<uint16_t>(~sum);
}
 
// Tinh va gan checksum cho 1 RDTPacket truoc khi gui.
// Chi tinh tren header (voi checksum=0) + payload_len byte dau cua payload
// (khong tinh phan rac phia sau payload chua dung den).
inline void fillChecksum(RDTPacket& pkt) {
    pkt.header.checksum = 0;
    uint16_t savedLen = pkt.header.payload_len;
 
    // Ghep tam header + payload thuc de tinh checksum
    static thread_local uint8_t buf[RDT_HEADER_SIZE + MAX_PAYLOAD_SIZE];
    std::memcpy(buf, &pkt.header, RDT_HEADER_SIZE);
    std::memcpy(buf + RDT_HEADER_SIZE, pkt.payload, savedLen);
 
    pkt.header.checksum = computeChecksum(buf, RDT_HEADER_SIZE + savedLen);
}
 
// Kiem tra 1 packet nhan duoc co bi corrupt khong.
inline bool verifyChecksum(const RDTPacket& pkt) {
    RDTHeader tmpHeader = pkt.header;
    uint16_t received = tmpHeader.checksum;
    tmpHeader.checksum = 0;
 
    static thread_local uint8_t buf[RDT_HEADER_SIZE + MAX_PAYLOAD_SIZE];
    std::memcpy(buf, &tmpHeader, RDT_HEADER_SIZE);
    std::memcpy(buf + RDT_HEADER_SIZE, pkt.payload, pkt.header.payload_len);
 
    return computeChecksum(buf, RDT_HEADER_SIZE + pkt.header.payload_len) == received;
}
 
// Chi gui dung so byte thuc su dang dung (header + payload_len), thay vi
// ca struct RDTPacket day du (~1400 byte du thua) -> tiet kiem bang thong.
// Dung ham nay truoc khi goi sendto().
inline size_t packForSend(const RDTPacket& pkt, uint8_t* outBuf, size_t outBufCap) {
    size_t total = RDT_HEADER_SIZE + pkt.header.payload_len;
    if (total > outBufCap) return 0; // buffer dua vao khong du cho
    std::memcpy(outBuf, &pkt.header, RDT_HEADER_SIZE);
    std::memcpy(outBuf + RDT_HEADER_SIZE, pkt.payload, pkt.header.payload_len);
    return total;
}
 
// Dung sau khi nhan tu recvfrom(): parse buffer tho thanh RDTPacket.
// Tra ve false neu du lieu qua ngan hoac payload_len vuot qua MAX_PAYLOAD_SIZE.
inline bool unpackFromRecv(const uint8_t* data, size_t len, RDTPacket& out) {
    if (len < RDT_HEADER_SIZE) return false;
    std::memcpy(&out.header, data, RDT_HEADER_SIZE);
    if (out.header.payload_len > MAX_PAYLOAD_SIZE) return false;
    if (len < RDT_HEADER_SIZE + out.header.payload_len) return false;
    std::memcpy(out.payload, data + RDT_HEADER_SIZE, out.header.payload_len);
    return true;
}
#endif 