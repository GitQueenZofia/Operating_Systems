# Operating_Systems

# tcp_1
Implementation of a simple router. Each host is assigned a number from 1 to 8, representing its address. The router always has the address 0. After connecting to the router, a host sends a message consisting of a single byte with the desired address. Communication between the client and the router is be done by sending messages of size at most MAX_BUF terminated with a '$' character. Hosts can be simulated using the Telnet program. Packets with larger sizes are rejected. 
If the router receives a packet with the recipient's address equal to 9, it sends the packet to every known host.
The router listens on a TCP socket. It is single-threaded and single-process. Hosts can be simulated as Telnet processes. </br>
The program accepts two arguments: the address to listen on and the socket port. The address will be the listening address, and the port is the socket port. 

# tcp_2
Implementation of a simple logging program that modifies and saves received packets. The modification mode is encoded in the first byte of the message. Total message length is encoded in the next two bytes. The remaining bytes represent the message to be logged. The received data should be saved to a file with the time of reception. In addition to logging the data, our program can process the data in three ways: </br>
1. Convert lowercase letters to uppercase and vice versa ("110AlaMaKota" becomes "aLAmAKOTA"). </br>
2. Replace all spaces with underscores and vice versa ("215_ Ala_Ma Kota " becomes "Ala Ma_Kota_"). </br>
3. Combination of the two above methods ("315_ Ala_Ma Kota " becomes "aLA mA_KOTA"). </br>

The logger listens on a selected TCP port for incoming connections. It allows only limited number of clients.
The program accepts three arguments: path to the log file, listening address, listening port.
Clients can be simulated using Telnet processes.
