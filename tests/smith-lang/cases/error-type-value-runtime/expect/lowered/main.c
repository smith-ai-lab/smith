typedef struct {
char name[8];
constexpr Type of;
} Column;
Type kinds[2] = {__INT32_TYPE__, float};
Column table = {"a", __UINT8_TYPE__};
int main() {
Type local = __INT32_TYPE__;
Column column = {"b", double};
constexpr Type Fine = __INT32_TYPE__;
constexpr Column also_fine = {"c", float};
return (int)sizeof(Fine) + (int)sizeof(also_fine.of);
}
