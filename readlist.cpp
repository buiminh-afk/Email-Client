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

const string serverIP = "192.168.56.1";
const int smtpPort = 2225;
const int pop3Port = 3335;

#define USER "USER your_username\r\n"
#define PASS "PASS your_password\r\n"
#define LIST "LIST\r\n"
#define RETR "RETR "
#define HELO "EHLO [127.0.0.1]\r\n"
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

// Function to send email headers
void send_email_headers(const Email& email) {
    // Add Date header
    time_t rawtime;
    struct tm * timeinfo;
    char buffer[80];
    time (&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer,sizeof(buffer),"%a, %d %b %Y %H:%M:%S %z",timeinfo);
    string dateHeader = "Date: ";
    dateHeader += buffer;
    dateHeader += "\r\n";
    send_socket(dateHeader.c_str());

    // // Add Message-ID header
    // string messageIDHeader = "Message-ID: <"; // You may generate a unique ID here
    // messageIDHeader += /*unique ID*/;
    // messageIDHeader += "@yourdomain.com>\r\n";
    // send_socket(messageIDHeader.c_str());

    // Add MIME-Version header
    string mimeVersionHeader = "MIME-Version: 1.0\r\n";
    send_socket(mimeVersionHeader.c_str());

    // Add User-Agent header
    string userAgentHeader = "User-Agent: Mozilla Thunderbird\r\n"; // You may specify your user agent here
    send_socket(userAgentHeader.c_str());
}

bool sendEmailSMTP(const string& serverIP, int port, const Email& email) {
    struct hostent *hp;

    string noAttachment = "Content-Type: text/plain; charset=UTF-8; format=flowed\r\nContent-Transfer-Encoding: 7bit\r\n\r\n";
    string temp = "";

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

    // SMTP communication-----------------------------------------------------------------------------------------------
    read_socket(); // SMTP Server logon string
    send_socket(HELO); // Introduce ourselves
    read_socket(); // Read reply

    // MAIL FROM-------------------------------------------------------------------------------------------------------------------------------------------
    send_socket("MAIL FROM: "); 
    temp = "<" + email.from + ">";
    send_socket(temp.c_str());
    send_socket("\r\n");
    read_socket(); // Sender OK

    // RCPT TO-------------------------------------------------------------------------------------------------------------------------------------------
    for (const string& recipient : email.to) {
        send_socket("RCPT TO: "); // Mail to
        temp = "<" + recipient + ">";
        send_socket(temp.c_str());
        send_socket("\r\n");
        read_socket(); // Recipient OK
    }

    // CC-------------------------------------------------------------------------------------------------------------------------------------------
    for (const string& recipient : email.cc) {
        send_socket("RCPT TO: "); // Mail to
        temp = "<" + recipient + ">";
        send_socket(temp.c_str());
        send_socket("\r\n");
        read_socket(); // Recipient OK
    }

    // BCC-------------------------------------------------------------------------------------------------------------------------------------------
    for (const string& recipient : email.bcc) {
        send_socket("RCPT TO: "); // Mail to
        temp = "<" + recipient + ">";
        send_socket(temp.c_str());
        send_socket("\r\n");
        read_socket(); // Recipient OK
    }

    //DATA--------------------------------------------------------------------------------------------------------------------------------------------------
    send_socket(DATA); // Body to follow
    read_socket(); // Recipient OK

    //HEADER--------------------------------------------------------------------------------------------------------------------------------------------------

    if (email.numAttachments != 0) {
        temp = "Content-Type: multipart/mixed; boundary=\"--boundary-type-1234567892-alt\"\r\n";
        send_socket(temp.c_str());
    }

    send_email_headers(email);

    temp = "TO: " + email.to[0];
    for (int i = 1; i < email.to.size(); i++) {
        temp += "," + email.to[i];
    }
    temp += "\r\n";
    send_socket(temp.c_str());

    temp = "FROM: <" + email.from + ">\r\n";
    send_socket(temp.c_str());

    temp = "Subject: " + email.subject + "\r\n";
    send_socket(temp.c_str());

    //if no attachment
    if (email.numAttachments != 0) {
        temp = "This is multi-part message in MIME format.\r\n--boundary-type-1234567892-alt\r\n";
        send_socket(temp.c_str());
    }
    send_socket(noAttachment.c_str());

    // Start of multipart message
    send_socket(email.content.c_str());
    send_socket("\r\n\r\n");

    // Send attachments
    for (const string& attachment : email.files) {
        send_socket("--boundary-type-1234567892-alt\r\n");
        temp = "Content-Type: text/plain; charset=UTF-8; name=\"" + attachment + "\"\r\n";
        temp += "Content-Disposition: attachment; filename=\"" + attachment + "\"\r\n";
        send_socket(temp.c_str());
        send_socket("Content-Transfer-Encoding: base64\r\n\r\n");

        // Read attachment content and encode to Base64
        ifstream file(attachment, ios::binary);
        if (file.is_open()) {
            stringstream buffer;
            buffer << file.rdbuf();
            file.close();
            string content = buffer.str();
            string encodedContent = base64::to_base64(content) + "\r\n";
            send_socket(encodedContent.c_str());
        } else {
            cerr << "Unable to open file: " << attachment << endl;
        }
    }

    // End of multipart message
    if (email.numAttachments != 0)
        send_socket("--boundary-type-1234567892-alt--\r\n");
    send_socket(".\r\n"); // End of data
    read_socket(); 
    send_socket(QUIT); // Quit
    read_socket(); // Log off

    return true;
}

// Function to send login credentials to the server and check response
bool login(const string& username, const string& password) {
    // Send username
    string userCommand = "USER " + username + "\r\n";
    send_socket(userCommand.c_str());
    read_socket(); // Read reply

    // Send password
    string passCommand = "PASS " + password + "\r\n";
    send_socket(passCommand.c_str());
    read_socket(); // Read reply

    // Read the response after sending password
    char buf[BUFSIZ+1];
    int len = read(sock, buf, BUFSIZ);
    buf[len] = '\0';
    string response(buf);

    // Check if login was successful
    if (response.find("+OK") != string::npos) {
        cout << "Login successful.\n";
        return true;
    } else {
        cout << "Login failed.\n";
        return false;
    }
}

// Function to receive email from POP3 server
void listEmail(const string& serverIP, int port, const string& username, const string& password) { 
    struct hostent *hp;

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

    // Connect to POP3 server
    memcpy((char *) &server.sin_addr, (char *) hp->h_addr, hp->h_length);
    server.sin_port = htons(port);
    if (connect(sock, (struct sockaddr *) &server, sizeof(server)) == -1) {
        perror("connecting stream socket");
        exit(1);
    } else {
        cout << "Connected to POP3 server\n";
    }


    if (login(username,password))

    // List emails
    send_socket(LIST);
    read_socket(); // Read reply
    
    /*
    // Retrieve emails
    // For example, let's retrieve the first email
    send_socket(RETR "1\r\n"); // Retrieve email with index 1
    read_socket(); // Read reply
    // Process email content here
    */
    send_socket(QUIT); // Quit
    read_socket(); // Log off
    
    // Close socket
    close(sock);
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
        cout << "So file muon gui: ";
        cin >> email.numAttachments;
        for (int i = 0; i < email.numAttachments; i++) {
            cout << "Nhap duong dan toi file muon gui: ";
            cin.ignore(); // Ignore the newline character
            getline(cin, temp, '\n');
            email.files.push_back(temp); // Encode file content to Base64 and add to the email
        }
        
    }
    return email;
}

void readEmail(const string& serverIP, int port, const string& username, const string& password, const string& index) {
    struct hostent *hp;

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

    // Connect to POP3 server
    memcpy((char *) &server.sin_addr, (char *) hp->h_addr, hp->h_length);
    server.sin_port = htons(port);
    if (connect(sock, (struct sockaddr *) &server, sizeof(server)) == -1) {
        perror("connecting stream socket");
        exit(1);
    } else {
        cout << "Connected to POP3 server\n";
    }


    if (login(username,password)){
    	//string temp = "<" + index + ">";
    	send_socket(RETR);
    	send_socket(index.c_str());
        send_socket("\r\n");
        read_socket(); // Recipient OK
        send_socket(QUIT); // Quit
    	read_socket(); // Log off
    
    // Close socket
    	close(sock);
    }
    else{
    send_socket(QUIT); // Quit
    read_socket(); // Log off
    
    // Close socket
    close(sock);
    cout << "out";
    }

}

int main() {
    // Nhập thông tin email từ người dùng
    // Email email = inputEmailInfo();

    // // Gửi email
    // bool sent = sendEmailSMTP(serverIP, smtpPort, email);
    // if (sent) {
    //     cout << "Email sent successfully.\n";
    // } else {
    //     cout << "Failed to send email.\n";
    // }

    string username, password;
    cout << "Username: ";
    cin >> username;
    cout << "Password: ";
    cin >> password;

    // Nhận email từ server POP3
    listEmail(serverIP, pop3Port, username, password);
    string index;
    cout << "Choose email to read: ";
    cin >> index;
    cout << index;
    readEmail(serverIP, pop3Port, username, password, index);
	
    // Đóng socket
    close(sock);

    return 0;
}   

