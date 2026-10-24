typedef struct {
__UINT8_TYPE__ tag;
__UINT64_TYPE__ value;
float weight;
} Packet;
struct Header {
__UINT16_TYPE__ id;
__UINT16_TYPE__ flags;
char name[6];
};
union Number {
__UINT32_TYPE__ whole;
float real;
};
typedef Packet Alias;
typedef struct Opaque Hidden;
int main() {
return 0;
}
