#include <iostream>
#include <string>
#include <vector>
#include <sstream> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <netdb.h>
#include <fstream>
#include "base64.hpp"

using namespace std;

const string serverIP = "10.124.2.236";
const int smtpPort = 2225;
const int pop3Port = 3335;

#define HELO "HELO 192.168.56.1\r\n"
#define DATA "DATA\r\n"
#define QUIT "QUIT\r\n"

int sock;
struct sockaddr_in server;

// Định nghĩa cấu trúc Email
struct Email {
    vector<string> to;
    string from;
    vector<string> cc;
    vector<string> bcc;
    string subject;
    string content;
    bool hasAttachment;
    int numAttachments;
    vector<string> files;
};

// Function to send data to the socket
void send_socket(const char *s) {
    write(sock, s, strlen(s));
    write(1, s, strlen(s)); // Echo to console
}

// Function to read data from the socket
void read_socket() {
    char buf[BUFSIZ+1];
    int len = read(sock, buf, BUFSIZ);
    write(1, buf, len); // Echo to console
}

bool sendEmailSMTP(const string& serverIP, int port, const Email& email) {
    struct hostent *hp;
    string attachmentHeader = "Content-Type: application/octet-stream\r\n";
    attachmentHeader += "Content-Disposition: attachment; filename=\"";

    // Create Socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("opening stream socket");
        exit(1);
    } else {
        cout << "Socket created\n";
    }

    // Verify host
    server.sin_family = AF_INET;
    hp = gethostbyname(serverIP.c_str());
    if (hp == nullptr) {
        fprintf(stderr, "%s: unknown host\n", serverIP.c_str());
        exit(2);
    }

    // Connect to SMTP server
    memcpy((char *) &server.sin_addr, (char *) hp->h_addr, hp->h_length);
    server.sin_port = htons(port);
    if (connect(sock, (struct sockaddr *) &server, sizeof(server)) == -1) {
        perror("connecting stream socket");
        exit(1);
    } else {
        cout << "Connected to SMTP server\n";
    }

    // SMTP communication
    read_socket(); // SMTP Server logon string
    send_socket(HELO); // Introduce ourselves
    read_socket(); // Read reply
    send_socket("MAIL FROM: "); 
    send_socket(email.from.c_str());
    send_socket("\r\n");
    read_socket(); // Sender OK
    for (const string& recipient : email.to) {
        send_socket("RCPT TO: "); // Mail to
        send_socket(recipient.c_str());
        send_socket("\r\n");
        read_socket(); // Recipient OK
    }
    send_socket(DATA); // Body to follow
    send_socket("Subject: ");
    send_socket(email.subject.c_str());
    send_socket("\r\n");
    send_socket("MIME-Version: 1.0\r\n");

    // Start of multipart message
    send_socket("Content-Type: multipart/mixed; boundary=boundary-type-1234567892-alt\r\n\r\n");

    // Email body
    send_socket("--boundary-type-1234567892-alt\r\n");
    send_socket("Content-Type: text/plain; charset=UTF-8\r\n");
    send_socket("Content-Transfer-Encoding: 7bit\r\n\r\n");
    send_socket(email.content.c_str());
    send_socket("\r\n\r\n");

    // Send attachments
    for (const string& attachment : email.files) {
        send_socket("--boundary-type-1234567892-alt\r\n");
        send_socket(attachmentHeader.c_str());
        send_socket(attachment.c_str());
        send_socket("\"\r\n");
        send_socket("Content-Transfer-Encoding: base64\r\n\r\n");

        // Read attachment content and encode to Base64
        ifstream file(attachment, ios::binary);
        if (file.is_open()) {
            stringstream buffer;
            buffer << file.rdbuf();
            file.close();
            string content = buffer.str();
            string encodedContent = base64::to_base64(content);
            send_socket(encodedContent.c_str());
        } else {
            cerr << "Unable to open file: " << attachment << endl;
        }

        send_socket("\r\n");
    }

    send_socket("--boundary-type-1234567892-alt--\r\n");
    send_socket(".\r\n"); // End of data
    read_socket(); 
    send_socket(QUIT); // Quit
    read_socket(); // Log off

    return true;
}



// Hàm nhập thông tin email từ người dùng
Email inputEmailInfo() {
    Email email;
    string temp;

    cout << "Day la thong tin soan email: (neu khong dien vui long nhan ENTER de bo qua)\n";
    cout << "From: ";
    getline(cin, email.from, '\n');
    cout << email.from << endl;

    cout << "To: ";
    getline(cin, temp, '\n');
    if (!temp.empty()) {
        stringstream toStream(temp);
        while (getline(toStream, temp, ',')) {
            email.to.push_back(temp);
        }
    }
    
    cout << "CC: ";
    getline(cin, temp, '\n');
    if (!temp.empty()) {
        stringstream toStream(temp);
        while (getline(toStream, temp, ',')) {
            email.cc.push_back(temp);
        }
    }

    cout << "BCC: ";
    getline(cin, temp, '\n');
    if (!temp.empty()) {
        stringstream toStream(temp);
        while (getline(toStream, temp, ',')) {
            email.bcc.push_back(temp);
        }
    }

    cout << "Subject: ";
    getline(cin,email.subject, '\n');

    cout << "Content: ";
    getline(cin,email.content, '\n');

    cout << "Co gui kem file (1. Co, 2. Khong): ";
    int attachmentOption;
    cin >> attachmentOption;
    email.hasAttachment = (attachmentOption == 1);
    if (email.hasAttachment) {
        cout << "Nhap duong dan toi file muon gui: ";
        cin.ignore(); // Ignore the newline character
        getline(cin, temp, '\n');
        email.files.push_back(temp); // Encode file content to Base64 and add to the email
        email.numAttachments = 1; // Set the number of attachments to 1
    }
    return email;
}



int main() {
    // Nhập thông tin email từ người dùng
    Email email = inputEmailInfo();

    // Gửi email
    bool sent = sendEmailSMTP(serverIP, smtpPort, email);
    if (sent) {
        cout << "Email sent successfully.\n";
    } else {
        cout << "Failed to send email.\n";
    }

    // Đóng socket
    close(sock);

    return 0;
}