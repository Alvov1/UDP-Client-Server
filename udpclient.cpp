#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <winsock2.h> // Директива линковщику: использовать библиотеку сокетов
    #pragma comment(lib, "ws2_32.lib")
#else // LINUX
#include <sys/poll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#endif

#include <cstdio>
#include <cstring>
#include <string>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <arpa/inet.h>
#include <set>

#ifdef _WIN32
    int flag = 0;
#else
    int flag = MSG_NOSIGNAL;
#endif

/* Количество отправленных строк */
unsigned lineCount = 0;
/* Множество для хранения ответов */
std::set<unsigned> answers;

bool deliveredSuccessfully = false;

union storage{
    char bites[4];
    unsigned short shortNumber;
    unsigned long longNumber;
};
/* ---------------------------------------------------------------------------- */
int sock_err(const char* function, int s) {
    int err;
#ifdef _WIN32
    err = WSAGetLastError();
#else
    err = errno;
#endif
    fprintf(stderr, "%s: socket error: %d\n", function, err);
    return -1;
}
int init() {
#ifdef _WIN32
    // Для Windows следует вызвать WSAStartup перед началом использования сокетов
    WSADATA wsa_data;
    return (0 == WSAStartup(MAKEWORD(2, 2), &wsa_data));
#else
    return 1; // Для других ОС действий не требуется
#endif
}
void deinit() {
#ifdef _WIN32
    // Для Windows следует вызвать WSACleanup в конце работы
    WSACleanup();
#else
    // Для других ОС действий не требуется
#endif
}
void send_request(int s, struct sockaddr_in* addr) {
    // Данные дейтаграммы DNS-запроса. Детальное изучение для л/р не требуется.
    char dns_datagram[] = {0x00, 0x00, 0x00, 0x00,
                           0x00, 0x01, 0x00, 0x00,
                           0x00, 0x00, 0x00, 0x00,
                           3, 'w', 'w','w', 6, 'y',
                           'a','n','d','e','x', 2,
                           'r', 'u', 0, 0x00, 0x01, 0x00, 0x01};

#ifdef _WIN32
    int flags = 0;
#else
    int flags = MSG_NOSIGNAL;
#endif

int res = sendto(s, dns_datagram, sizeof(dns_datagram), flags, (struct sockaddr*) addr, sizeof(struct sockaddr_in));
if (res <= 0)
    sock_err("sendto", s);
}
// Функция принимает дейтаграмму от удаленной стороны.
// Возвращает 0, если в течение 100 миллисекунд не было получено ни одной дейтаграммы
unsigned int recv_response(int s) {
    char datagram[1024];
    struct timeval tv = {0, 100 * 1000}; // 100 msec
    int res;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(s, &fds);

    // Проверка - если в сокете входящие дейтаграммы
    // (ожидание в течение tv)
    res = select(s + 1, &fds, 0, 0, &tv);
    if (res > 0) {
        // Данные есть, считывание их
        struct sockaddr_in addr{};
        socklen_t addrlen = sizeof(addr);
        int received = recvfrom(s, datagram, sizeof(datagram), 0, (struct sockaddr *) &addr, &addrlen);
        if (received <= 0) {
            // Ошибка считывания полученной дейтаграммы
            return sock_err("recvfrom", s);
        }

        /* ---------------------------------------------------------------------------- */
        storage data{};
        for(int i = 0; i * 4 < received; i++){
            memcpy(data.bites, datagram + i * 4, 4);
            unsigned messageNumber = ntohl(data.longNumber);
            /* Получили новый ответ на одно из отправленных сообщений? */
            if (lineCount >= messageNumber && answers.find(messageNumber) == answers.end()) {
                std::cout << "Responce on message " << messageNumber << std::endl;
                answers.insert(messageNumber);
            }
        }
        /* ---------------------------------------------------------------------------- */
        return 0;

    } else if (res == 0) {
        // Данных в сокете нет, возврат ошибки
        std::cout << "Got zero Responces" << std::endl;
        return 0;
    } else {
        sock_err("select", s);
        return 0;
    }
}
void s_close(int s)
{
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}
/* ---------------------------------------------------------------------------- */
std::string lineParse(const std::string &line){
    storage data{};
    std::cout << "Sending line " << lineCount << ": " << line << std::endl;

    /* Message number */
    data.longNumber = htonl(lineCount);
    std::string message;    // 4-bite number
/**/message.assign(data.bites, 4);
    data.longNumber = 0;

    const std::string date = line.substr(0, line.find(' '));

    unsigned pos = date.find('.');
    const std::string day = date.substr(0, pos);
    const std::string month = date.substr(pos + 1, date.find('.', pos + 1) - pos - 1);
    pos = date.find('.', pos + 1);
    const std::string year = date.substr(pos + 1);

    /* dd.mm.yyyy */
    data.shortNumber = stoi(day);
/**/message += std::string(1, data.bites[0]);    // 1-bite number
    data.shortNumber = stoi(month);
/**/message += std::string(1, data.bites[0]);    // 1-bite number
    data.shortNumber = htons(stoi(year));
/**/message += std::string(data.bites).substr(0, 2);    // 2-bite number

    /* AA */
    pos = line.find(' ');
    std::string number = line.substr(pos, line.find(' ', pos + 1) - pos);
    data.shortNumber = htons(stoi(number));
    number.assign(data.bites, 2);
/**/message += number;    // 2-bite number


    /* +7xxxxxxxxxx */
    pos = line.find(' ', pos + 1) + 2;
/**/message += "+7" + line.substr(pos + 1, 10);

    /* Message */
    pos = line.find(' ', pos);
/**/message += line.substr(pos + 1);

    return message;
}

int main(int argc, char** argv) {

    if(argc != 3){
        std::cout << "Error with arguments" << std::endl;
        return -1;
    }
    struct sockaddr_in addr{};

    // Инициалиазация сетевой библиотеки
    init();

    std::string secondArg = std::string(*(argv + 1));
    std::string ip = secondArg.substr(0, secondArg.find(':'));
    std::string port = secondArg.substr(secondArg.find(':') + 1);
    std::cout << "IP Address - " << ip << ',' << " Port - " << port << std::endl;

    // Создание UDP-сокета
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        return sock_err("socket", s);

    // Заполнение структуры с адресом удаленного узла
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(stoi(port)); // Порт DNS - 53
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    /* ---------------------------------------------------------------------------- */

    std::string filename(argv[2]);
    std::ifstream file(filename);
    if(file.fail()){
        std::cout << "Error with '" << filename << "' file" << std::endl;
        s_close(s);
        deinit();
        return 0;
    }
    std::string line;


    while(true){
        lineCount = 0;
        file.seekg(0);
        /* Send file */
        while (!file.eof() && answers.size() < 20) {
            /* Прекращаем отправку, когда доставлено 20 и более сообщений,
             * или доставлены все сообщения из файла, если в нем их меньше 20. */
            getline(file, line);
            if(line.size() != 0 && line[line.size() - 1] == '\r')
                line.erase(line.size() - 1);

            if(line.size() != 0) {
                /* Данное сообщение еще не доставлено? */
                if (answers.find(lineCount) == answers.end()) {
                    std::string message = lineParse(line);
                    sendto(s, message.c_str(), message.size() + 1, flag, (struct sockaddr *) &addr,
                           sizeof(struct sockaddr_in));

                    /* Counting responces */
                    if(recv_response(s) == -1){
                        std::cout << "Error with responces" << std::endl;
                        return -1;
                    }
                }

                lineCount++;
            }
        }

        if(lineCount == answers.size() || answers.size() >= 20){
            file.close();
            deliveredSuccessfully = true;
            break;
        }
    }

    if(deliveredSuccessfully)
        std::cout << "File delivered successfully" << std::endl;
    else
        std::cout << "File sent, but got only " << answers.size() << " responces" << std::endl;
    /* ---------------------------------------------------------------------------- */
    s_close(s);
    deinit();
    return 0;
}
