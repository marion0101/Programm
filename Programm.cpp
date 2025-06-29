#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <csignal>
#include <linux/if.h>
#include <linux/sockios.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

// Конфигурационные параметры
const int MONITOR_INTERVAL = 2; // интервал мониторинга в секундах
const int MAX_PACKET_RATE = 1000; // максимальный допустимый пакет/сек
const int MAX_BANDWIDTH = 100; // максимальный допустимый трафик МБ/сек

// Глобальные флаги
volatile sig_atomic_t stop_flag = 0;

// Обработчик сигналов для graceful shutdown
void signal_handler(int signum) {
    stop_flag = 1;
    cout << "\nПолучен сигнал завершения, останавливаю мониторинг..." << endl;
}

// Структура для хранения статистики интерфейса
struct InterfaceStats {
    string name;
    long long rx_bytes;
    long long tx_bytes;
    long long rx_packets;
    long long tx_packets;
    long long rx_errors;
    long long tx_errors;
};

// Функция получения статистики сетевого интерфейса
InterfaceStats get_interface_stats(const string& interface) {
    InterfaceStats stats{interface, 0, 0, 0, 0, 0, 0};
    string path = "/sys/class/net/" + interface + "/statistics/";
    
    ifstream rx_bytes_file(path + "rx_bytes");
    ifstream tx_bytes_file(path + "tx_bytes");
    ifstream rx_packets_file(path + "rx_packets");
    ifstream tx_packets_file(path + "tx_packets");
    ifstream rx_errors_file(path + "rx_errors");
    ifstream tx_errors_file(path + "tx_errors");
    
    if (rx_bytes_file) rx_bytes_file >> stats.rx_bytes;
    if (tx_bytes_file) tx_bytes_file >> stats.tx_bytes;
    if (rx_packets_file) rx_packets_file >> stats.rx_packets;
    if (tx_packets_file) tx_packets_file >> stats.tx_packets;
    if (rx_errors_file) rx_errors_file >> stats.rx_errors;
    if (tx_errors_file) tx_errors_file >> stats.tx_errors;
    
    return stats;
}

// Функция получения информации о сетевом интерфейсе
void get_interface_info(const string& interface) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interface.c_str(), IFNAMSIZ-1);

    // Получаем IP-адрес
    if (ioctl(fd, SIOCGIFADDR, &ifr) == 0) {
        struct sockaddr_in* ipaddr = (struct sockaddr_in*)&ifr.ifr_addr;
        cout << "IP адрес: " << inet_ntoa(ipaddr->sin_addr) << endl;
    }

    // Получаем маску подсети
    if (ioctl(fd, SIOCGIFNETMASK, &ifr) == 0) {
        struct sockaddr_in* netmask = (struct sockaddr_in*)&ifr.ifr_netmask;
        cout << "Маска подсети: " << inet_ntoa(netmask->sin_addr) << endl;
    }

    // Получаем MTU
    if (ioctl(fd, SIOCGIFMTU, &ifr) == 0) {
        cout << "MTU: " << ifr.ifr_mtu << endl;
    }

    // Получаем индекс интерфейса
    if (ioctl(fd, SIOCGIFINDEX, &ifr) == 0) {
        cout << "Индекс интерфейса: " << ifr.ifr_ifindex << endl;
    }

    close(fd);
}

// Функция мониторинга сетевого интерфейса
void monitor_interface(const string& interface) {
    cout << "Мониторинг интерфейса " << interface << "..." << endl;
    get_interface_info(interface);

    InterfaceStats prev_stats = get_interface_stats(interface);
    auto prev_time = chrono::steady_clock::now();

    while (!stop_flag) {
        this_thread::sleep_for(chrono::seconds(MONITOR_INTERVAL));
        
        InterfaceStats curr_stats = get_interface_stats(interface);
        auto curr_time = chrono::steady_clock::now();
        double elapsed = chrono::duration<double>(curr_time - prev_time).count();
        
        // Рассчитываем скорости
        long long rx_bytes_diff = curr_stats.rx_bytes - prev_stats.rx_bytes;
        long long tx_bytes_diff = curr_stats.tx_bytes - prev_stats.tx_bytes;
        long long rx_packets_diff = curr_stats.rx_packets - prev_stats.rx_packets;
        long long tx_packets_diff = curr_stats.tx_packets - prev_stats.tx_packets;
        
        double rx_rate = rx_bytes_diff / elapsed / (1024 * 1024); // МБ/сек
        double tx_rate = tx_bytes_diff / elapsed / (1024 * 1024); // МБ/сек
        double rx_packet_rate = rx_packets_diff / elapsed;
        double tx_packet_rate = tx_packets_diff / elapsed;
        
        cout << "\n--- Статистика за последние " << elapsed << " сек. ---" << endl;
        cout << "Прием: " << rx_rate << " МБ/сек, " << rx_packet_rate << " пакетов/сек" << endl;
        cout << "Передача: " << tx_rate << " МБ/сек, " << tx_packet_rate << " пакетов/сек" << endl;
        cout << "Ошибки приема: " << curr_stats.rx_errors - prev_stats.rx_errors << endl;
        cout << "Ошибки передачи: " << curr_stats.tx_errors - prev_stats.tx_errors << endl;
        
        // Проверка на превышение параметров
        if (rx_packet_rate > MAX_PACKET_RATE || tx_packet_rate > MAX_PACKET_RATE) {
            cerr << "ВНИМАНИЕ: Превышена максимальная скорость пакетов!" << endl;
            // Здесь можно добавить действия при превышении
        }
        
        if (rx_rate > MAX_BANDWIDTH || tx_rate > MAX_BANDWIDTH) {
            cerr << "ВНИМАНИЕ: Превышена максимальная пропускная способность!" << endl;
            // Здесь можно добавить действия при превышении
        }
        
        prev_stats = curr_stats;
        prev_time = curr_time;
    }
}

int main() {
    // Установка обработчика сигналов
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    cout << "Сетевой монитор v1.0" << endl;
    
    // Получаем список сетевых интерфейсов
    vector<string> interfaces;
    ifstream netdev("/proc/net/dev");
    
    if (netdev) {
        string line;
        while (getline(netdev, line)) {
            size_t pos = line.find(':');
            if (pos != string::npos) {
                string iface = line.substr(0, pos);
                iface.erase(remove_if(iface.begin(), iface.end(), ::isspace), iface.end());
                if (!iface.empty()) {
                    interfaces.push_back(iface);
                }
            }
        }
    }
    
    cout << "\nДоступные сетевые интерфейсы:" << endl;
    for (size_t i = 0; i < interfaces.size(); ++i) {
        cout << i+1 << ". " << interfaces[i] << endl;
    }
    
    if (interfaces.empty()) {
        cerr << "Не найдено сетевых интерфейсов!" << endl;
        return 1;
    }
    
    cout << "\nВыберите интерфейс для мониторинга (1-" << interfaces.size() << "): ";
    size_t choice;
    cin >> choice;
    
    if (choice < 1 || choice > interfaces.size()) {
        cerr << "Неверный выбор!" << endl;
        return 1;
    }
    
    string selected_iface = interfaces[choice-1];
    monitor_interface(selected_iface);
    
    cout << "Мониторинг завершен." << endl;
    return 0;
}
