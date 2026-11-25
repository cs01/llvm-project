// icmptunnel test client. The payload that pointed at a literal becomes an array.
struct icmp_packet { char src[16], dest[16]; int type; char payload[64]; int size; };
int open_icmp_socket(void);
void send_icmp_packet(int fd, const struct icmp_packet *p);
void close_icmp_socket(int fd);
int copy(char *__counted_by(n) to, int n, const char *from) { int i = 0; for (; *from && i < n - 1; from++) to[i++] = *from; to[i] = 0; return i; }
int main(void) {
  struct icmp_packet packet = {0};
  copy(packet.src, 16, "127.0.0.2");
  copy(packet.dest, 16, "127.0.0.1");
  packet.type = 1;
  packet.size = copy(packet.payload, 64, "ZZZZZZ");
  int fd = open_icmp_socket();
  send_icmp_packet(fd, &packet);
  close_icmp_socket(fd);
  return 0;
}
