struct geo_units_Request {
__INT32_TYPE__ id;
};
typedef struct geo_units_Request geo_units_Request;
constexpr type geo_units_Meters = double;
constexpr type geo_units_Handler = __INT32_TYPE__ (*)(geo_units_Request *);
constexpr type geo_units_Block = __UINT8_TYPE__[4];
constexpr type geo_units_Row = __INT32_TYPE__ (*)[4];
constexpr type geo_units_Pointers = geo_units_Request *[2];
constexpr type geo_feet = float;
geo_units_Meters geo_units_add(geo_units_Meters a, geo_units_Meters b) __asm__("_geo::units::add");
geo_units_Meters geo_units_add(geo_units_Meters a, geo_units_Meters b) {
return a + b;
}
__INT32_TYPE__ geo_units_identify(geo_units_Request *request) __asm__("_geo::units::identify");
__INT32_TYPE__ geo_units_identify(geo_units_Request *request) {
return request->id;
}
geo_units_Handler geo_units_handler() __asm__("_geo::units::handler");
geo_units_Handler geo_units_handler() {
return geo_units_identify;
}
