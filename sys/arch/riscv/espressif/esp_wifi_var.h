/* $NetBSD$ */

#ifndef _RISCV_ESPRESSIF_ESP_WIFI_VAR_H_
#define _RISCV_ESPRESSIF_ESP_WIFI_VAR_H_

void	espwifi_attach(void);
int	espwifi_if_attach(void);
void	espwifi_if_input(const void *, unsigned int);
void	espwifi_if_link(int, const unsigned char *);
int	espwifi_if_assoc_response(unsigned int, const void *, unsigned int,
	    const unsigned char *);
bool	espwifi_if_wpa_in_4way(void);
void	espwifi_if_scan_frame(const void *, unsigned, unsigned, int, unsigned);
void	espwifi_if_radio_ready(void);
void	espwifi_if_radio_started(void);
void	espwifi_if_radio_stopped(void);
int	espwifi_if_rx_eapol(const unsigned char *, const void *, unsigned);

#endif
