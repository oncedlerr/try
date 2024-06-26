#include "WardenFort.h"
#include "ui_WardenFort.h"
#include <QColor>
#include <QTableWidgetItem>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <QApplication>
#include <QThread>
#include <iphlpapi.h>    
#include <ctime>
#include <QScrollBar>
#include <pcap.h>
#include <QDebug> 
#include "accountsettings.h"
#include "passwordsec.h"
#include "login.h"
#include "loginsession.h"

#pragma comment(lib, "Ws2_32.lib")

int i = 0;
int j = 0;
int k = 0;
int portScanningDetected = 0;
int DOSDetected = 0;

WardenFort::WardenFort(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::WardenFort)
{
    ui->setupUi(this);

    // Connect the clicked signal of dd buttons to the toggle function
    connect(ui->dd1, &QPushButton::clicked, this, &WardenFort::toggleButtons);
    connect(ui->dd2, &QPushButton::clicked, this, &WardenFort::toggleButtons);
    connect(ui->dd3, &QPushButton::clicked, this, &WardenFort::toggleButtons);
    connect(ui->dd4, &QPushButton::clicked, this, &WardenFort::toggleButtons);
    connect(ui->dd5, &QPushButton::clicked, this, &WardenFort::toggleButtons);
    connect(ui->dd6, &QPushButton::clicked, this, &WardenFort::toggleButtons);
    connect(ui->dd7, &QPushButton::clicked, this, &WardenFort::toggleButtons);
    connect(ui->dd8, &QPushButton::clicked, this, &WardenFort::toggleButtons);

    //profiletab links
    connect(ui->passwordButton, &QPushButton::released, this, &WardenFort::on_passwordButton_released);
    connect(ui->accountButton, &QPushButton::released, this, &WardenFort::on_accountButton_released);
    connect(ui->logoutButton, &QPushButton::released, this, &WardenFort::on_logoutButton_released);

    // Initially hide dd5 to dd8 buttons
    ui->dd5->setVisible(false);
    ui->dd6->setVisible(false);
    ui->dd7->setVisible(false);
    ui->dd8->setVisible(false);
    ui->profileTab_2->setVisible(false);
    ui->profileTab_3->setVisible(false);
    ui->profileTab_4->setVisible(false);
    ui->profileTab_5->setVisible(false);
    ui->frame_2->setVisible(false);

    ui->tableWidget->setColumnWidth(0, 120);
    ui->tableWidget->setColumnWidth(1, 60);
    ui->tableWidget->setColumnWidth(2, 80);
    ui->tableWidget->setColumnWidth(3, 50);
    ui->tableWidget->setColumnWidth(4, 50);
    ui->tableWidget->setColumnWidth(6, 30);
    ui->tableWidget->setColumnWidth(7, 250);

    loginsession* log = new loginsession;
    QString Name = log->username;

    ui->welcome_text->setText(Name);
}

WardenFort::~WardenFort()
{
    delete ui;
}


void WardenFort::settrafficAnomalies(const QString& text) {
    ui->trafficAnomalies->setText(text);
}

void WardenFort::setcriticalAnomalies(const QString& text) {
    ui->criticalDetections->setText(text);
}

void WardenFort::setOverallAlert(const QString& text) {
    ui->overallAlert->setText(text);
}

QTableWidget* WardenFort::getTableWidget() {
    return ui->tableWidget;

}

void WardenFort::setWelcomeText(const QString& text) {
    ui->welcome_text->setText(text);
}

// Define TCP header flags
#define TH_FIN  0x01
#define TH_SYN  0x02
#define TH_RST  0x04
#define TH_PUSH 0x08
#define TH_ACK  0x10
#define TH_URG  0x20

constexpr int MAX_EXPECTED_PAYLOAD_LENGTH = 9999; // Maximum expected payload length
constexpr int MIN_EXPECTED_PAYLOAD_LENGTH = 0;    // Minimum expected payload length (can be adjusted as needed)
constexpr int MAX_EXPECTED_PACKET_LENGTH = 512; // Maximum expected packet length
constexpr int MAX_PACKET_LENGTH_THRESHOLD = 512; // Threshold for detecting unusually large packets
constexpr int MAX_DOS_DETECTED_THRESHOLD = 10;    // Threshold for DoS attack detection
constexpr int MAX_NORMAL_TRANSFER_SIZE = 512;      // Threshold for data exfiltration

struct IPHeader {
    u_char  VersionAndHeaderLength;  // Version (4 bits) + Header length (4 bits)
    u_char  TypeOfService;           // Type of service
    u_short TotalLength;             // Total length
    u_short Identification;          // Identification
    u_short FlagsAndFragmentOffset;  // Flags (3 bits) + Fragment offset (13 bits)
    u_char  TimeToLive;              // Time to live
    u_char  Protocol;                // Protocol
    u_short HeaderChecksum;          // Header checksum
    u_long  Source;                  // Source address
    u_long  Destination;             // Destination address
};

struct ICMPHeader {
    uint8_t type;      // ICMP message type
    uint8_t code;      // ICMP message code
    uint16_t checksum; // ICMP message checksum
                       // Additional fields if needed
};

struct my_tcphdr {
    u_short th_sport;   // source port
    u_short th_dport;   // destination port
    u_int th_seq;       // sequence number
    u_int th_ack;       // acknowledgement number
    u_char th_offx2;    // data offset, rsvd
    u_char th_flags;
    u_short th_win;     // window
    u_short th_sum;     // checksum
    u_short th_urp;     // urgent pointer
};

bool isFilteredAdapter(pcap_if_t* adapter) {
    // Check if adapter is a WAN miniport or VirtualBox adapter by description
    if (adapter->description) {
        QString description = QString(adapter->description).toLower();
        if (description.contains("wan miniport") || description.contains("virtualbox"))
            return true;
    }

    // Check if adapter is a loopback adapter (npf_loopback)
    if (adapter->flags & PCAP_IF_LOOPBACK)
    {
        return true;
    }

    // Check if adapter is inactive
    if (!(adapter->flags & PCAP_IF_CONNECTION_STATUS_CONNECTED))
        {
        return true;
    }

    return false;
}

void analyzeTCPPacket(const u_char* packet, int packetLength, WardenFort& wardenFort) {

    // Ensure the packet is large enough to contain IP and TCP headers
    if (packetLength < sizeof(IPHeader) + sizeof(struct my_tcphdr)) {
        qDebug() << "Packet is too small to contain IP and TCP headers";
        return;
    }

    // Extract IP header from packet
    const IPHeader* ipHeader = reinterpret_cast<const IPHeader*>(packet);

    // Extract TCP header from packet
    const struct my_tcphdr* tcpHeader = reinterpret_cast<const struct my_tcphdr*>(packet + sizeof(IPHeader));

    // Convert source and destination IP addresses from network to presentation format
    char sourceIp[INET_ADDRSTRLEN];
    char destIp[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(ipHeader->Source), sourceIp, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(ipHeader->Destination), destIp, INET_ADDRSTRLEN);

    // Convert source and destination port numbers from network to host byte order
    u_short sourcePort = ntohs(tcpHeader->th_sport);
    u_short destPort = ntohs(tcpHeader->th_dport);

    // Calculate TCP header length
    int tcpHeaderLength = (tcpHeader->th_offx2 >> 4) * 4;

    // Calculate payload length
    int payloadLength = packetLength - sizeof(IPHeader) - tcpHeaderLength;

    // Analyze payload length
    // Analyze TCP flags
    // Analyze other aspects of the TCP packet as needed
    // For example, you could check sequence numbers, window size, etc.
}

void packetHandler(WardenFort* wardenFort, const struct pcap_pkthdr* pkthdr, const u_char* packet) {
    bool isVeryBottom = false;

    qDebug() << "Packet captured. Length:" << pkthdr->len;

    struct tm* timeinfo;
    char buffer[80];
    time_t packet_time = static_cast<time_t>(pkthdr->ts.tv_sec);
    packet_time += 28800;
    timeinfo = gmtime(&packet_time);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    QString timestamp = QString(buffer);

    QString packetInfo = "Timestamp: " + timestamp + "\n";
    packetInfo += "Packet length: " + QString::number(pkthdr->len) + " bytes\n";
    packetInfo += "Captured length: " + QString::number(pkthdr->caplen) + " bytes\n";

    const IPHeader* ipHeader = reinterpret_cast<const IPHeader*>(packet);
    const struct my_tcphdr* tcpHeader = reinterpret_cast<const struct my_tcphdr*>(packet + sizeof(IPHeader));

    char sourceIp[INET_ADDRSTRLEN];
    char destIp[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(ipHeader->Source), sourceIp, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(ipHeader->Destination), destIp, INET_ADDRSTRLEN);

    QString sourcePort = QString::number(ntohs(tcpHeader->th_sport));
    QString destPort = QString::number(ntohs(tcpHeader->th_dport));
    QString flags;
    if (tcpHeader->th_flags & TH_SYN) flags += "SYN ";
    if (tcpHeader->th_flags & TH_ACK) flags += "ACK ";
    if (tcpHeader->th_flags & TH_FIN) flags += "FIN ";

    int tcpHeaderLength = (tcpHeader->th_offx2 >> 4) * 4;

    int payloadLength = pkthdr->caplen - sizeof(IPHeader) - tcpHeaderLength;

    QString info = QString("No Information");

    if (ipHeader->Protocol == IPPROTO_ICMP) {
        int totalLength = ntohs(ipHeader->TotalLength);
        int icmpHeaderLength = sizeof(ICMPHeader);
        int icmpPayloadLength = totalLength - sizeof(IPHeader) - icmpHeaderLength;
        constexpr int MAX_ICMP_PAYLOAD_LENGTH = 10;
        if (icmpPayloadLength > MAX_ICMP_PAYLOAD_LENGTH) {
            info = QString("Large ICMP Echo Request detected. Payload Length: %1").arg(icmpPayloadLength);
        }
    }

    if (tcpHeader->th_flags & TH_SYN && !(tcpHeader->th_flags & TH_ACK)) {
        if (ntohs(tcpHeader->th_dport) >= 1 && ntohs(tcpHeader->th_dport) <= 1024) {
            info = QString("Port Scanning Detected. Source: %1 Destination Port: %2").arg(sourceIp).arg(destPort);
        }
    }

    if (pkthdr->caplen >= MAX_EXPECTED_PACKET_LENGTH) {
        info = QString("Possible Denial of Service (DoS) Attack Detected. Packet Length: %1").arg(pkthdr->caplen);
    }else if (pkthdr->caplen >= MAX_PACKET_LENGTH_THRESHOLD) {
        info = QString("Unusually large packet detected. Packet Length: %1").arg(pkthdr->caplen);
    }

    if (DOSDetected > MAX_DOS_DETECTED_THRESHOLD) {
        info = QString("DoS attack threshold exceeded. Total DoS attacks detected: %1").arg(DOSDetected);
    }

    if (payloadLength > MIN_EXPECTED_PAYLOAD_LENGTH && payloadLength < MAX_EXPECTED_PAYLOAD_LENGTH) {
        u_short destPort = ntohs(tcpHeader->th_dport);
        switch (destPort) {
        case 6667:
            info = QString("Malware communication detected (IRC-based malware). Destination port: %1").arg(destPort);
            break;
        default:
            const char* payload = reinterpret_cast<const char*>(tcpHeader) + sizeof(struct my_tcphdr);
            if (strstr(payload, "malicious_pattern")) {
                info = QString("Malware communication detected (suspicious payload content). Destination port: %1").arg(destPort);
            }
            break;
        }
    }

    if (payloadLength > MIN_EXPECTED_PAYLOAD_LENGTH && payloadLength < MAX_EXPECTED_PAYLOAD_LENGTH) {
        const char* payload = reinterpret_cast<const char*>(tcpHeader) + sizeof(struct my_tcphdr);
        if (strstr(payload, "confidential_data_pattern")) {
            info = QString("Data exfiltration detected. Suspicious pattern found in payload.");
        }
        else if (payloadLength > MAX_NORMAL_TRANSFER_SIZE) {
            info = QString("Data exfiltration detected. Unusually large payload size.");
        }
    }

    if (tcpHeader->th_flags & TH_SYN && !(tcpHeader->th_flags & TH_ACK)) {
        if (ntohs(tcpHeader->th_dport) >= 1 && ntohs(tcpHeader->th_dport) <= 1024) {
            info = QString("Network Reconnaissance Detected. Source: %1 Destination Port:%2").arg(sourceIp).arg(destPort);
        }
    }

    if (tcpHeader->th_flags & (TH_FIN | TH_SYN | TH_RST | TH_PUSH | TH_ACK | TH_URG)) {
        int flagCount = 0;
        flagCount += tcpHeader->th_flags & TH_FIN ? 1 : 0;
        flagCount += tcpHeader->th_flags & TH_SYN ? 1 : 0;
        flagCount += tcpHeader->th_flags & TH_RST ? 1 : 0;
        flagCount += tcpHeader->th_flags & TH_PUSH ? 1 : 0;
        flagCount += tcpHeader->th_flags & TH_ACK ? 1 : 0;
        flagCount += tcpHeader->th_flags & TH_URG ? 1 : 0;

        if (flagCount != 1 && !(tcpHeader->th_flags & (TH_SYN | TH_ACK)) && !(tcpHeader->th_flags & (TH_FIN | TH_ACK))) {
            // Protocol anomaly detected
            info = QString("Protocol Anomaly Detected. Source: %1 Destination: %2").arg(sourceIp).arg(destIp);
        }
    }

    QTableWidget* tableWidget = wardenFort->getTableWidget();
    int row = tableWidget->rowCount();
    tableWidget->insertRow(row);
    tableWidget->setItem(row, 0, new QTableWidgetItem(timestamp));
    tableWidget->setItem(row, 1, new QTableWidgetItem(QString(sourceIp)));
    tableWidget->setItem(row, 2, new QTableWidgetItem(QString(destIp)));
    tableWidget->setItem(row, 3, new QTableWidgetItem(sourcePort));
    tableWidget->setItem(row, 4, new QTableWidgetItem(destPort));
    tableWidget->setItem(row, 5, new QTableWidgetItem(flags));
    tableWidget->setItem(row, 6, new QTableWidgetItem(QString::number(pkthdr->caplen)));
    tableWidget->setItem(row, 7, new QTableWidgetItem(info));

    if (payloadLength > MAX_EXPECTED_PAYLOAD_LENGTH) {
        wardenFort->settrafficAnomalies(QString::number(i));
        k = i + j;
        wardenFort->setOverallAlert(QString::number(k));
        for (int col = 0; col < tableWidget->columnCount(); ++col) {
            tableWidget->item(row, col)->setBackground(QColor(73, 75, 44));
        }
    }

    if (tcpHeader->th_flags & TH_SYN && !(tcpHeader->th_flags & TH_ACK)) {
        wardenFort->settrafficAnomalies(QString::number(i));
        k = i + j;
        wardenFort->setOverallAlert(QString::number(k));
        for (int col = 0; col < tableWidget->columnCount(); ++col) {
            tableWidget->item(row, col)->setBackground(QColor(67, 75, 44));
        }
    }

    if (payloadLength < MIN_EXPECTED_PAYLOAD_LENGTH) {
        wardenFort->settrafficAnomalies(QString::number(i));
        k = i + j;
        wardenFort->setOverallAlert(QString::number(k));
        for (int col = 0; col < tableWidget->columnCount(); ++col) {
            tableWidget->item(row, col)->setBackground(QColor(62, 75, 44)); // Change row color to red
        }
    }

    // Check if the packet is an ICMP packet
    if (ipHeader->Protocol == IPPROTO_ICMP) {
        // Calculate the total length of the packet
        int totalLength = ntohs(ipHeader->TotalLength);

        // Calculate the length of the ICMP header
        int icmpHeaderLength = sizeof(ICMPHeader);

        // Calculate the length of the ICMP payload
        int icmpPayloadLength = totalLength - sizeof(IPHeader) - icmpHeaderLength;

        // Check if the ICMP payload length exceeds a certain threshold
        constexpr int MAX_ICMP_PAYLOAD_LENGTH = 10; // Adjust as needed
        if (icmpPayloadLength > MAX_ICMP_PAYLOAD_LENGTH) {
            // If the ICMP payload is too large, log the event or take appropriate action
            qDebug() << "Large ICMP Echo Request detected. Payload Length:" << icmpPayloadLength;
            wardenFort->settrafficAnomalies(QString::number(i));
            k = i + j;
            wardenFort->setOverallAlert(QString::number(k));
            // Add code here to log the event or take appropriate action
            // Change row color to red
            for (int col = 0; col < tableWidget->columnCount(); ++col) {
                tableWidget->item(row, col)->setBackground(QColor(44, 75, 68));
            }
        }
    }

    // Port scanning detector
    if (tcpHeader->th_flags & TH_SYN && !(tcpHeader->th_flags & TH_ACK)) {
        // Check if the destination port is in a range commonly used for scanning
        if (ntohs(tcpHeader->th_dport) >= 1 && ntohs(tcpHeader->th_dport) <= 1024) {
            // Port scanning detected
            qDebug() << "Port Scanning Detected. Source:" << sourceIp << " Destination Port:" << destPort;
            j++;
            portScanningDetected++;
            wardenFort->setcriticalAnomalies(QString::number(j));
            k = i + j;
            wardenFort->setOverallAlert(QString::number(k));

            for (int col = 0; col < tableWidget->columnCount(); ++col) {
                tableWidget->item(row, col)->setBackground(QColor(61, 44, 75));
            }
        }
    }

    if (pkthdr->caplen >= MAX_EXPECTED_PACKET_LENGTH) {
        // DoS attack suspected due to excessively large packet size
        qDebug() << "Possible Denial of Service (DoS) Attack Detected. Packet Length:" << pkthdr->caplen;

        // Increment counters
        j++;
        wardenFort->setcriticalAnomalies(QString::number(j));
        k = i + j;
        wardenFort->setOverallAlert(QString::number(k));

        // Log the event or take appropriate action

        // Change row color to denote potential DoS attack
        for (int col = 0; col < tableWidget->columnCount(); ++col) {
            tableWidget->item(row, col)->setBackground(QColor(75, 44, 69)); // Change row color to red
        }
    }

    // Check if the packet length exceeds the threshold for unusually large packets
    else if (pkthdr->caplen >= MAX_PACKET_LENGTH_THRESHOLD) {
        // Log the event or take appropriate action
        qDebug() << "Unusually large packet detected. Packet Length:" << pkthdr->caplen;
        i++;
        wardenFort->settrafficAnomalies(QString::number(i));
        k = i + j;
        wardenFort->setOverallAlert(QString::number(k));
        // Add code here to handle the event, such as logging or alerting

        for (int col = 0; col < tableWidget->columnCount(); ++col) {
            tableWidget->item(row, col)->setBackground(QColor(75, 44, 62)); // Change row color to red
        }
    }

    if (payloadLength > MIN_EXPECTED_PAYLOAD_LENGTH && payloadLength < MAX_EXPECTED_PAYLOAD_LENGTH) {
        // Analyze destination port for known malware communication ports
        u_short destPort = ntohs(tcpHeader->th_dport);
        switch (destPort) {
        case 6667: // Example port used by IRC-based malware
            qDebug() << "Malware communication detected (IRC-based malware). Destination port:" << destPort;
            j++;
            wardenFort->setcriticalAnomalies(QString::number(j));
            k = i + j;
            wardenFort->setOverallAlert(QString::number(k));
            for (int col = 0; col < tableWidget->columnCount(); ++col) {
                tableWidget->item(row, col)->setBackground(QColor(75, 44, 44)); // Change row color to red
            }
            break;
            // Add more cases for other known malware communication ports as needed
        default:
            // Check for suspicious payload content
            const char* payload = reinterpret_cast<const char*>(tcpHeader) + sizeof(struct my_tcphdr);
            if (strstr(payload, "malicious_pattern")) {
                qDebug() << "Malware communication detected (suspicious payload content). Destination port:" << destPort;
                j++;
                wardenFort->setcriticalAnomalies(QString::number(j));
                k = i + j;
                wardenFort->setOverallAlert(QString::number(k));
                for (int col = 0; col < tableWidget->columnCount(); ++col) {
                    tableWidget->item(row, col)->setBackground(QColor(75, 44, 44)); // Change row color to red
                }
            }
            break;
        }
    }

    if (payloadLength > MIN_EXPECTED_PAYLOAD_LENGTH && payloadLength < MAX_EXPECTED_PAYLOAD_LENGTH) {
        // Analyze payload content for suspicious data transfer patterns
        const char* payload = reinterpret_cast<const char*>(tcpHeader) + sizeof(struct my_tcphdr);
        // Example: Check for presence of sensitive keywords or patterns
        if (strstr(payload, "confidential_data_pattern")) {
            qDebug() << "Data exfiltration detected. Suspicious pattern found in payload.";
            i++;
            wardenFort->settrafficAnomalies(QString::number(i));
            k = i + j;
            wardenFort->setOverallAlert(QString::number(k));
            for (int col = 0; col < tableWidget->columnCount(); ++col) {
                tableWidget->item(row, col)->setBackground(QColor(75, 44, 44)); // Change row color to red
            }
        }
        // Example: Check for unusually high data transfer volume
        else if (payloadLength > MAX_NORMAL_TRANSFER_SIZE) {
            qDebug() << "Data exfiltration detected. Unusually large payload size.";
            i++;
            wardenFort->settrafficAnomalies(QString::number(i));
            k = i + j;
            wardenFort->setOverallAlert(QString::number(k));
            for (int col = 0; col < tableWidget->columnCount(); ++col) {
                tableWidget->item(row, col)->setBackground(QColor(75, 44, 44)); // Change row color to red
            }
        }
        // Add more detection criteria as needed based on your network and security policies
    }

    if (tcpHeader->th_flags & TH_SYN && !(tcpHeader->th_flags & TH_ACK)) {
        // Check if the destination port is commonly used for scanning
        if (ntohs(tcpHeader->th_dport) >= 1 && ntohs(tcpHeader->th_dport) <= 1024) {
            // Network reconnaissance detected
            qDebug() << "Network Reconnaissance Detected. Source:" << sourceIp << " Destination Port:" << destPort;
            i++;
            wardenFort->settrafficAnomalies(QString::number(i));
            k = i + j;
            wardenFort->setOverallAlert(QString::number(k));
            for (int col = 0; col < tableWidget->columnCount(); ++col) {
                tableWidget->item(row, col)->setBackground(QColor(75, 44, 44)); // Change row color to red
            }
        }
    }

    if (tcpHeader->th_flags & (TH_FIN | TH_SYN | TH_RST | TH_PUSH | TH_ACK | TH_URG)) {
        // Flags should not have multiple bits set, except for SYN-ACK or FIN-ACK
        int flagCount = 0;
        flagCount += tcpHeader->th_flags & TH_FIN ? 1 : 0;
        flagCount += tcpHeader->th_flags & TH_SYN ? 1 : 0;
        flagCount += tcpHeader->th_flags & TH_RST ? 1 : 0;
        flagCount += tcpHeader->th_flags & TH_PUSH ? 1 : 0;
        flagCount += tcpHeader->th_flags & TH_ACK ? 1 : 0;
        flagCount += tcpHeader->th_flags & TH_URG ? 1 : 0;

        if (flagCount != 1 && !(tcpHeader->th_flags & (TH_SYN | TH_ACK)) && !(tcpHeader->th_flags & (TH_FIN | TH_ACK))) {
            // Protocol anomaly detected
            qDebug() << "Protocol Anomaly Detected. Source:" << sourceIp << " Destination:" << destIp;
            i++;
            wardenFort->settrafficAnomalies(QString::number(i));
            k = i + j;
            wardenFort->setOverallAlert(QString::number(k));
            for (int col = 0; col < tableWidget->columnCount(); ++col) {
                tableWidget->item(row, col)->setBackground(QColor(75, 44, 44)); // Change row color to red
            }
        }
    }

    // Print overall packet information
    qDebug().noquote() << packetInfo << "Source IP:" << sourceIp << "Destination IP:" << destIp << "Source Port:" << sourcePort << "Destination Port:" << destPort << "Flags:" << flags << "Captured length:" << pkthdr->caplen;
    qDebug() << portScanningDetected << DOSDetected;
    // Analyze the TCP packet
    analyzeTCPPacket(packet, pkthdr->len, *wardenFort);

    //always scroll to the bottom

}

void packetHandlerWrapper(u_char* user, const struct pcap_pkthdr* pkt_header, const u_char* pkt_data) {
    WardenFort* wardenFort = reinterpret_cast<WardenFort*>(user);
    packetHandler(wardenFort, pkt_header, pkt_data);
}

void captureTCPPackets(pcap_if_t* adapter, WardenFort& wardenFort) {
    char errbuf[PCAP_ERRBUF_SIZE];

    // Open the adapter for live capturing
    pcap_t* pcapHandle = pcap_open_live(adapter->name, 65535, 1, 1000, errbuf);
    if (pcapHandle == nullptr) {
        qDebug() << "Error opening adapter for capturing:" << errbuf;
        return;
    }

    // Apply a filter to capture only TCP packets
    struct bpf_program filter;
    if (pcap_compile(pcapHandle, &filter, "tcp", 0, PCAP_NETMASK_UNKNOWN) == -1) {
        qDebug() << "Error compiling filter:" << pcap_geterr(pcapHandle);
        pcap_close(pcapHandle);
        return;
    }
    if (pcap_setfilter(pcapHandle, &filter) == -1) {
        qDebug() << "Error setting filter:" << pcap_geterr(pcapHandle);
        pcap_freecode(&filter);
        pcap_close(pcapHandle);
        return;
    }
    pcap_freecode(&filter);

    // Start capturing packets
    if (pcap_loop(pcapHandle, 0, packetHandlerWrapper, reinterpret_cast<u_char*>(&wardenFort)) == -1) {
        qDebug() << "Error capturing packets:" << pcap_geterr(pcapHandle);
    }

    // Close the capture handle when done
    pcap_close(pcapHandle);
}

class CaptureThread : public QThread {
public:
    explicit CaptureThread(pcap_if_t* adapter, WardenFort& wardenFort) : adapter(adapter), wardenFort(wardenFort) {}

protected:
    void run() override {
        captureTCPPackets(adapter, wardenFort);
    }

private:
    pcap_if_t* adapter;
    WardenFort& wardenFort;
};

void WardenFort::scanActiveLANAdapters() { // Corrected definition
    char errbuf[PCAP_ERRBUF_SIZE];

    // Get a list of all available network adapters
    pcap_if_t* allAdapters;
    if (pcap_findalldevs(&allAdapters, errbuf) == -1) {
        qDebug() << "Error finding adapters:" << errbuf;
        return;
    }

    // Iterate over the list of adapters
    for (pcap_if_t* adapter = allAdapters; adapter != nullptr; adapter = adapter->next) {
        if (adapter->name != nullptr && !isFilteredAdapter(adapter)) {
            // Display the name and description of the LAN adapter
            qDebug() << "Active LAN adapter found:" << adapter->name << "Description:" << (adapter->description ? adapter->description : "No Description");
            

            // Start capturing TCP packets from this adapter in a separate thread
            CaptureThread* thread = new CaptureThread(adapter, *this); // Pass reference to this object
            thread->start();
        }
    }

    // Free the list of adapters
    pcap_freealldevs(allAdapters);
}

void WardenFort::toggleButtonVisibility(QPushButton* buttonToHide, QPushButton* buttonToShow)
{
    buttonToHide->setVisible(false);
    buttonToShow->setVisible(true);
}

void WardenFort::toggleButtons()
{
    int newY;
    QPushButton* clickedButton = qobject_cast<QPushButton*>(sender());
    if (!clickedButton)
        return; // Safety check

    if (clickedButton == ui->dd1) {
        // Remove the border-top style
        ui->profileTab->setStyleSheet("QFrame#profileTab { background-color: rgba(44, 60, 75, 0); border-radius: 0px; }");

        toggleButtonVisibility(ui->dd1, ui->dd5);
        //alerts tab
        newY = ui->alertsTab->y() + 90;
        ui->alertsTab->move(ui->alertsTab->x(), newY);
        //reports tab
        newY = ui->reportsTab->y() + 90;
        ui->reportsTab->move(ui->reportsTab->x(), newY);
        //calendarTab
        newY = ui->calendarTab->y() + 90;
        ui->calendarTab->move(ui->calendarTab->x(), newY);

        ui->profileTab_2->setVisible(true);
        ui->frame_2->setVisible(true);
    }

    else if (clickedButton == ui->dd2) {
        ui->profileTab->setStyleSheet("QFrame#profileTab { background-color: rgba(44, 60, 75, 0); border-radius: 0px; }");

        toggleButtonVisibility(ui->dd2, ui->dd6);

        //reports tab
        newY = ui->reportsTab->y() + 65;
        ui->reportsTab->move(ui->reportsTab->x(), newY);
        //calendarTab
        newY = ui->calendarTab->y() + 65;
        ui->calendarTab->move(ui->calendarTab->x(), newY);
        ui->profileTab_3->setVisible(true);
        ui->frame_2->setVisible(true);
    }
    else if (clickedButton == ui->dd3) {
        ui->profileTab->setStyleSheet("QFrame#profileTab { background-color: rgba(44, 60, 75, 0); border-radius: 0px; }");
        toggleButtonVisibility(ui->dd3, ui->dd7);
        //calendarTab
        newY = ui->calendarTab->y() + 42;
        ui->calendarTab->move(ui->calendarTab->x(), newY);
        ui->profileTab_4->setVisible(true);
        ui->frame_2->setVisible(true);
    }
    else if (clickedButton == ui->dd4) {
        ui->profileTab->setStyleSheet("QFrame#profileTab { background-color: rgba(44, 60, 75, 0); border-radius: 0px; }");
        toggleButtonVisibility(ui->dd4, ui->dd8);
        ui->profileTab_5->setVisible(true);
        ui->frame_2->setVisible(true);
    }
    else if (clickedButton == ui->dd5) {
        ui->profileTab->setStyleSheet("QFrame#profileTab { background-color: rgba(44, 60, 75, 0); border-radius: 0px; border-top: 1px solid rgb(25, 31, 50);}");

        toggleButtonVisibility(ui->dd5, ui->dd1);
        //alerts tab
        newY = ui->alertsTab->y() - 90;
        ui->alertsTab->move(ui->alertsTab->x(), newY);
        //reports tab
        newY = ui->reportsTab->y() - 90;
        ui->reportsTab->move(ui->reportsTab->x(), newY);
        //calendarTab
        newY = ui->calendarTab->y() - 90;
        ui->calendarTab->move(ui->calendarTab->x(), newY);
        ui->profileTab_2->setVisible(false);
        ui->frame_2->setVisible(false);
    }
    else if (clickedButton == ui->dd6) {
        ui->profileTab->setStyleSheet("QFrame#profileTab { background-color: rgba(44, 60, 75, 0); border-radius: 0px; border-top: 1px solid rgb(25, 31, 50);}");

        toggleButtonVisibility(ui->dd6, ui->dd2);
        //reports tab
        newY = ui->reportsTab->y() - 65;
        ui->reportsTab->move(ui->reportsTab->x(), newY);
        //calendarTab
        newY = ui->calendarTab->y() - 65;
        ui->calendarTab->move(ui->calendarTab->x(), newY);
        ui->profileTab_3->setVisible(false);
        ui->frame_2->setVisible(false);
    }
    else if (clickedButton == ui->dd7) {
        ui->profileTab->setStyleSheet("QFrame#profileTab { background-color: rgba(44, 60, 75, 0); border-radius: 0px; border-top: 1px solid rgb(25, 31, 50);}");
        toggleButtonVisibility(ui->dd7, ui->dd3);
        //calendarTab
        newY = ui->calendarTab->y() - 42;
        ui->calendarTab->move(ui->calendarTab->x(), newY);
        ui->profileTab_4->setVisible(false);
        ui->frame_2->setVisible(false);
    }
    else if (clickedButton == ui->dd8) {
        ui->profileTab->setStyleSheet("QFrame#profileTab { background-color: rgba(44, 60, 75, 0); border-radius: 0px; border-top: 1px solid rgb(25, 31, 50);}");
        toggleButtonVisibility(ui->dd8, ui->dd4);
        ui->profileTab_5->setVisible(false);
        ui->frame_2->setVisible(false);
    }
}

void WardenFort::on_passwordButton_released() {

    ui->passwordButton->setStyleSheet("QPushButton { font: 8pt \"Inter\"; background-color: transparent; color: white; }"
        "QPushButton:pressed { background-color: lightgray; }");
    passwordSec* passWindow = new passwordSec;
     passWindow->getUsername(ui->welcome_text->text());
    this->close();
    passWindow->show();
    disconnect(ui->passwordButton, &QPushButton::released, this, &WardenFort::on_passwordButton_released);

}

void WardenFort::on_accountButton_released() {

    ui->accountButton->setStyleSheet("QPushButton { font: 8pt \"Inter\"; background-color: transparent; color: white; }"
        "QPushButton:pressed { background-color: lightgray; }");
    accountSettings* accountWindow = new accountSettings;
    this->close();
    accountWindow->show();
    disconnect(ui->accountButton, &QPushButton::released, this, &WardenFort::on_accountButton_released);
}

void WardenFort::on_logoutButton_released() {

    ui->logoutButton->setStyleSheet("QPushButton { font: 8pt \"Inter\"; background-color: transparent; color: red; }"
        "QPushButton:pressed { background-color: #FFA07A; }");
    login* loginWindow = new login;
    this->close();
    loginWindow->show();
    disconnect(ui->logoutButton, &QPushButton::released, this, &WardenFort::on_logoutButton_released);
}





