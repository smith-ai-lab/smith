typedef struct {
char name[8];
constexpr type of;
} Column;
type kinds[2] = {__INT32_TYPE__, float};
Column table = {"a", __UINT8_TYPE__};
int main() {
type local = __INT32_TYPE__;
Column column = {"b", double};
constexpr type fine = __INT32_TYPE__;
constexpr Column also_fine = {"c", float};
return (int)sizeof(fine) + (int)sizeof(also_fine.of);
}
