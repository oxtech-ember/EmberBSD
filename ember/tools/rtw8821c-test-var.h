#include <stdint.h>
struct rtw88_bus_ops {
	int (*read_1)(void *, uint32_t, uint8_t *);
	int (*read_4)(void *, uint32_t, uint32_t *);
	int (*write_1)(void *, uint32_t, uint8_t);
	int (*write_2)(void *, uint32_t, uint16_t);
	int (*write_4)(void *, uint32_t, uint32_t);
	void (*delay_us)(void *, unsigned);
};
struct rtw88_softc {
	const struct rtw88_bus_ops *sc_ops;
	void *sc_bus;
	unsigned sc_stop_requested;
	uint8_t sc_cut;
};
