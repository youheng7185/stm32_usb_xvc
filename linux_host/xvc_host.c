#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libusb-1.0/libusb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>

#define VENDOR_ID    0x6cf0
#define PRODUCT_ID   0x0050

#define BULK_EP_OUT  0x01  // adjust if yours is different
#define BULK_EP_IN   0x81  // adjust if yours is different
#define TIMEOUT_MS   1000

#define PORT 2542
#define BACKLOG 1
#define BUFFER_SIZE 8192  // Larger buffer
#define MAX_WRITE_SIZE 2048  // Increased from 512

libusb_context *ctx = NULL;
libusb_device_handle *handle = NULL;
int rc; // return code
int transferred;

int server_fd, client_fd;
struct sockaddr_in addr;
uint8_t tcp_buffer[BUFFER_SIZE];
int tcp_buffer_len = 0;
char reply[64];

void create_tcp_socket() {
    // Create TCP socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Set socket options for performance
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR");
    }
    
    // Set larger socket buffers
    int bufsize = 65536;
    if (setsockopt(server_fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize)) < 0) {
        perror("setsockopt SO_RCVBUF");
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize)) < 0) {
        perror("setsockopt SO_SNDBUF");
    }

    // Bind to port 2542 on all interfaces
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Listen for a connection
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Listening on port %d...\n", PORT);

    // Accept a single connection
    client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
        perror("accept");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Client connected.\n");
}

void bulk_transfer_out(uint8_t *buffer, uint32_t len) {
    rc = libusb_bulk_transfer(handle, BULK_EP_OUT, buffer, (int)len, &transferred, TIMEOUT_MS);
    if (rc == 0 && transferred == (int)len) {
        // Remove verbose logging for speed
    } else {
        fprintf(stderr, "Send failed: %s\n", libusb_error_name(rc));
    }
}

void bulk_transfer_in(uint8_t *buffer, uint32_t len) {
    rc = libusb_bulk_transfer(handle, BULK_EP_IN, buffer, (int)len, &transferred, TIMEOUT_MS);
    if (rc != 0) {
        fprintf(stderr, "Receive failed: %s\n", libusb_error_name(rc));
    }
}

// Helper function to receive exactly n bytes from TCP socket
int recv_exact(int sockfd, uint8_t *buffer, int len) {
    int total_received = 0;
    int bytes_received;
    
    while (total_received < len) {
        bytes_received = recv(sockfd, buffer + total_received, len - total_received, 0);
        if (bytes_received <= 0) {
            return bytes_received; // Error or connection closed
        }
        total_received += bytes_received;
    }
    return total_received;
}

int main(void) {
    
    rc = libusb_init(&ctx);
    if (rc < 0) {
        fprintf(stderr, "libusb init failed: %s\n", libusb_error_name(rc));
        return EXIT_FAILURE;
    }

    handle = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
    if (!handle) {
        fprintf(stderr, "Device not found\n");
        libusb_exit(ctx);
        return EXIT_FAILURE;
    }

    // Detach kernel driver if needed
    if (libusb_kernel_driver_active(handle, 0)) {
        libusb_detach_kernel_driver(handle, 0);
    }

    rc = libusb_claim_interface(handle, 0);
    if (rc < 0) {
        fprintf(stderr, "Failed to claim interface: %s\n", libusb_error_name(rc));
        libusb_close(handle);
        libusb_exit(ctx);
        return EXIT_FAILURE;
    }

    create_tcp_socket();

    // Optimized main loop - back to simpler approach but with safety checks
    while(1) {
        // Try to read command header first
        int len = recv(client_fd, tcp_buffer, 10, 0); // Read at least command header
        if (len <= 0) {
            printf("Client disconnected\n");
            break;
        }
        
        tcp_buffer_len = len;

        if (memcmp(tcp_buffer, "getinfo:", 8) == 0) {
            printf("xvc info\n");
            snprintf(reply, sizeof(reply), "xvcServer_v1.0:%d\n", MAX_WRITE_SIZE);
            send(client_fd, reply, strlen(reply), 0);
            
        } else if (memcmp(tcp_buffer, "settck:", 7) == 0) {
            // Make sure we have the full 4-byte parameter
            if (len < 11) {
                int extra = recv_exact(client_fd, tcp_buffer + len, 11 - len);
                if (extra <= 0) break;
                tcp_buffer_len = 11;
            }
            
            uint32_t period = tcp_buffer[7] | (tcp_buffer[8] << 8) | 
                             (tcp_buffer[9] << 16) | (tcp_buffer[10] << 24);
            printf("xvc tck, requested freq: %d\n", period);
            send(client_fd, &tcp_buffer[7], 4, 0);
            
        } else if (memcmp(tcp_buffer, "shift:", 6) == 0) {
            // Make sure we have the bit count
            if (len < 10) {
                int extra = recv_exact(client_fd, tcp_buffer + len, 10 - len);
                if (extra <= 0) break;
                tcp_buffer_len = 10;
            }
            
            uint32_t num_bits = tcp_buffer[6] | (tcp_buffer[7] << 8) | 
                               (tcp_buffer[8] << 16) | (tcp_buffer[9] << 24);
            uint32_t num_bytes = (num_bits + 7) / 8;
            
            // printf("Shift: %d bits (%d bytes)\n", num_bits, num_bytes);
            
            // Receive TMS and TDI data in one go for speed
            uint8_t *data_buffer = malloc(2 * num_bytes);
            if (!data_buffer) {
                fprintf(stderr, "Memory allocation failed\n");
                break;
            }
            
            if (recv_exact(client_fd, data_buffer, 2 * num_bytes) <= 0) {
                free(data_buffer);
                break;
            }
            
            // Send to USB device - all at once for better performance
            bulk_transfer_out(&tcp_buffer[6], sizeof(uint32_t));  // Send num_bits
            bulk_transfer_out(data_buffer, num_bytes);            // Send TMS data
            bulk_transfer_out(data_buffer + num_bytes, num_bytes); // Send TDI data

            // Receive response from USB device
            uint8_t *response_buffer = malloc(num_bytes);
            if (!response_buffer) {
                free(data_buffer);
                fprintf(stderr, "Memory allocation failed\n");
                break;
            }
            
            bulk_transfer_in(response_buffer, num_bytes);
            
            // Send response back to TCP client
            send(client_fd, response_buffer, num_bytes, 0);
            
            free(data_buffer);
            free(response_buffer);
            
        } else {
            printf("Unknown command: ");
            for (int i = 0; i < len && i < 16; i++) {
                printf("%02X ", tcp_buffer[i]);
            }
            printf("\n");
        }
    }

    close(client_fd);
    close(server_fd);
    libusb_release_interface(handle, 0);
    libusb_close(handle);
    libusb_exit(ctx);

    return 0;
}