struct Point {
int x;
int y;
};
typedef int import;
import global_value = 7;
int main(void) {
struct Point c = {1, 2};
import value = c.x + c.y;
int module = value;
int export_count = module;
printf("%d %d %d\n", value, export_count, global_value);
return 0;
}
