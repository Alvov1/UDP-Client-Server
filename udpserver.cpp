#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h> // Директива линковщику: использовать библиотеку сокетов
#pragma comment(lib, "ws2_32.lib")

#include <cstdio>
#include <cstring>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <set>

#define BUFFERSIZE 400000
/* Ожидаемое максимальное число клиентов */
#define CLIENTS_COUNT 25
int flags = 0;

void s_close(int s)
{
    closesocket(s);
}

class Client {
public:
    explicit Client(const int &socket = 0, const int port = 0, sockaddr_in* addr = nullptr) : socket(socket), port(port), addr(addr){}
    int giveSocket() const {
        return this->socket;
    }
    sockaddr_in* giveAddr() const {
        return this->addr;
    }

    ~Client(){
        s_close(this->socket);
    }

    void dellSocket() const {
        s_close(this->socket);
    }

private:
    const int socket;
    const unsigned port;
    sockaddr_in* addr;
};

class MessagesThroughPort{
public:
    explicit MessagesThroughPort(const unsigned ip = 0, const unsigned port = 0) : ip(ip), port(port) {}
    unsigned givePort() const{
        return this->port;
    }
    unsigned giveIP() const{
        return this->ip;
    }
    int addNumber(const unsigned messageNumber){
        if(this->database.find(messageNumber) == this->database.end()) {
            this->database.insert(messageNumber);
            return 0;
        } else return -1;   /* От этого клиента уже получено сообщение с этим номером */
    }
private:
    std::set<unsigned> database;
    unsigned port;
    unsigned ip;
};

MessagesThroughPort* find(std::vector<MessagesThroughPort*> database, const unsigned ip, const unsigned port){
    for(unsigned i = 0; i < database.size(); i++)
        if(database[i]->giveIP() == ip && database[i]->givePort() == port)
            return database[i];
    return nullptr;
}

/* Массив для хранения данных клиентов */
std::vector<Client*> database;
/* Массив для отслеживания номеров сообщений */
std::vector<MessagesThroughPort*> messages;
/* Получена служебная строка 'stop'? */
bool shutdownFlag = false;
/* Номер последнего полученного сообщения */
unsigned lastMessageNumber = 1000;
/* ---------------------------------------------------------------------------- */
int set_non_block_mode(int s){
    unsigned long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode);
}
int sock_err(const char* function, int s) {
    int err;
    err = WSAGetLastError();
    fprintf(stderr, "%s: socket error: %d\n", function, err);
    return -1;
}
int init() {
    // Для Windows следует вызвать WSAStartup перед началом использования сокетов
    WSADATA wsa_data;
    return (0 == WSAStartup(MAKEWORD(2, 2), &wsa_data));
}
void deinit() {
    // Для Windows следует вызвать WSACleanup в конце работы
    WSACleanup();
}
/* ---------------------------------------------------------------------------- */

union storage{
    char bites[4];
    unsigned short unsignedShortNumber;
    short shortNumber;
    unsigned long longNumber;
};

std::string ipToString(const unsigned ipNumber){
    long long* tempNum = new long long[4];
    *tempNum = (ipNumber >> 24) & 0xFF;
    *(tempNum + 1) = (ipNumber >> 16) & 0xFF;
    *(tempNum + 2) = (ipNumber >> 8) & 0xFF;
    *(tempNum + 3) = (ipNumber) & 0xFF;
    std::string ip = std::to_string(*tempNum) + "."
                     + std::to_string(tempNum[1]) + "."
                     + std::to_string(tempNum[2]) + "."
                     + std::to_string(tempNum[3]);
    delete [] tempNum;
    tempNum = nullptr;
    return ip;
}

std::string getMessage(const char *buffer) {
    std::string message;
    static storage data;

    /* Number of message */
    memcpy(data.bites, buffer, 4 * sizeof(char));
    lastMessageNumber = ntohl(data.longNumber);
    buffer += 4;

    /* dd.mm.yyyy AA +7xxxxxxxxxx Message  */

    /* Date */
    const std::string day = std::to_string(static_cast<long long>(*buffer));
    const std::string month = std::to_string(static_cast<long long>(*(buffer + 1)));
    data.longNumber = 0;
    memcpy(data.bites, buffer + 2, 2 * sizeof(char));
    const std::string year = std::to_string(static_cast<long long>(ntohs(data.shortNumber)));
    buffer += 4;

    if(day.size() == 1)
        message = "0" + day + ".";
    else
        message = day + ".";

    if(month.size() == 1)
        message += "0" + month + ".";
    else
        message += month + ".";

    for(int i = 0; i < 4 - year.size(); i++)
        message += "0";

    message += year + " ";

    /* AA */
    memcpy(data.bites, buffer, 2 * sizeof(char));
    data.shortNumber = ntohs(data.shortNumber);
    message += std::to_string(static_cast<long long>(data.shortNumber)) + " ";
    buffer += 2;
    data.shortNumber = 0;

    /* Phone number */
    char* phoneBuffer = new char[13];
    memcpy(phoneBuffer, buffer, 12 * sizeof(char));
    phoneBuffer[12] = 0;
    message += std::string(phoneBuffer) + " ";
    buffer += 12;
    delete [] phoneBuffer;
    phoneBuffer = nullptr;
    const unsigned len = message.size();

    /* Message */
    while(*buffer != 0){
        message += *buffer;
        buffer++;
    }

    /* Shutdown */
    if(message.substr(len) == "stop")
        shutdownFlag = true;

    return message;
}

void sendResponce(const Client *client){
    static storage data;

    /* Responce */
    data.longNumber = htonl(lastMessageNumber);
    sendto(client->giveSocket(), data.bites, 4, flags,
           (struct sockaddr*) client->giveAddr(), sizeof(*(client->giveAddr())));
}

int shutdownServer(std::ofstream &file){
    // Закрытие сокета
    std::cout << "'stop' message arrived. Terminating..." << std::endl;
    for(int i = 0; i < database.size(); i++)
        database[i]->dellSocket();
    if(file.is_open()) file.close();
    deinit();
    database.clear();
    deinit();
    return 0;
}

int main(int argc, char** argv) {
    if(argc != 3){
        std::cout << "Error with arguments" << std::endl;
        return -1;
    }

    // Инициалиазация сетевой библиотеки
    init();

    std::string lowestPort(argv[1]);
    std::string highestPort(argv[2]);
    const int range = stoi(highestPort) - stoi(lowestPort);
    if(range <= 0){
        std::cout << "Error with arguments" << std::endl;
        return -1;
    }
    database.reserve(range + 1);
    messages.reserve(CLIENTS_COUNT);

    for(unsigned i = 0; i < range + 1; i++){
        struct sockaddr_in* addr = new sockaddr_in;

        // Создание UDP-сокета
        const int s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s < 0)
            return sock_err("socket", s);

        set_non_block_mode(s);

        // Заполнение структуры с адресом прослушивания узла
        memset(addr, 0, sizeof(sockaddr_in));
        addr->sin_family = AF_INET;
        addr->sin_port = htons(stoi(lowestPort) + i); // Будет прослушиваться порт 8000
        addr->sin_addr.s_addr = htonl(INADDR_ANY);

        // Связь адреса и сокета, чтобы он мог принимать входящие дейтаграммы
        if (bind(s, (struct sockaddr *) addr, sizeof(*addr)) < 0)
            return sock_err("bind", s);

        /* Создаем объект класса Клиент и добавляем его в массив */
        Client *temp = new Client(s, stoi(lowestPort) + i, addr);
        database.push_back(temp);
    }

    std::cout << "Listening ports " << lowestPort << " - " << highestPort << std::endl;

    WSAEVENT events; // Первое событие - прослушивающего сокета(6 сокетов), второе - клиентских соединений
    events = WSACreateEvent();
    for (int i = 0; i < database.size(); i++)//Сопоставили прослушивающим сокетам событие
        WSAEventSelect(database[i]->giveSocket(), events, FD_READ | FD_WRITE | FD_CLOSE);

    const std::string filename("msg.txt");
    std::ofstream file(filename);

    char buffer[BUFFERSIZE] = {0};

    while (true) {
        WSANETWORKEVENTS ne;
        // Ожидание событий в течение секунды
        DWORD dw = WSAWaitForMultipleEvents(1, &events, FALSE, 1, FALSE);
        WSAResetEvent(events);

        for (int i = 0; i < database.size(); i++) {
            Client *tempClient = database[i];

            if (0 == WSAEnumNetworkEvents(tempClient->giveSocket(), events, &ne)) {
                // По сокету cs[i] есть события
                if (ne.lNetworkEvents & FD_READ) {
                    // Есть данные для чтения, можно вызывать recv/recvfrom на cs[i]

                    sockaddr_in* giveAddr = tempClient->giveAddr();
                    int addrlen = sizeof(*giveAddr);
                    int rcv = recvfrom(tempClient->giveSocket(), buffer, sizeof(buffer),
                                       flags, (struct sockaddr*) giveAddr, &addrlen);
                    if(rcv > 0){
                        std::string message = getMessage(buffer);
                        if(!message.empty()){
                            const unsigned ip = ntohl(giveAddr->sin_addr.S_un.S_addr);
                            const unsigned port = giveAddr->sin_port;
                            std::string ipStr = ipToString(ip);
                            std::string portStr = std::to_string(static_cast<long long>(port));

                            if(find(messages, ip, port) == nullptr) {
                                /* От этого порта еще не поступало сообщений */
                                MessagesThroughPort* temp = new MessagesThroughPort(ip, port);
                                temp->addNumber(lastMessageNumber);
                                messages.push_back(temp);
                            } else
                                if(find(messages, ip, port)->addNumber(lastMessageNumber) == -1)
                                    continue;

                            std::cout << ipStr << ":" << portStr << " Message " << lastMessageNumber << ": " << message << std::endl;
                            file << ipStr << ":" << portStr << " " << message;

                            sendResponce(tempClient);

                            if(!shutdownFlag)
                                file << std::endl;
                            else
                                return shutdownServer(file);
                        }
                    }
                    if(shutdownFlag)
                        return shutdownServer(file);
                }
                if (ne.lNetworkEvents & FD_CLOSE) {
                    // Удаленная сторона закрыла соединение, можно закрыть сокет и удалить его из cs

                    sockaddr_in* giveAddr = database[i]->giveAddr();
                    std::string ip = ipToString(ntohl(giveAddr->sin_addr.S_un.S_addr));

                    std::string port = std::to_string(static_cast<long long>(ntohl(giveAddr->sin_port)));
                    std::cout << "Client " << ip << ":" << port << "disconnected" << std::endl;

                    database[i]->dellSocket();
                }

                if(shutdownFlag)
                    return shutdownServer(file);
            }
        }
        if(shutdownFlag)
            return shutdownServer(file);
    }
    return 0;
}