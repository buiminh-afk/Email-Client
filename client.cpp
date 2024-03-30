#include <iostream>
#include <string>
#include <vector>
#include <sstream> 
#include <sys/ioctl.h>
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
const string username = "buinguyennhatminh911@gmail.com";
const string password = "09112003Minh!!\\";
int last_email_id = 0;


#define HELO "HELO 192.168.56.1\r\n"
#define USER "USER your_username\r\n"
#define PASS "PASS your_password\r\n"
#define LIST "LIST\r\n"
#define RETR "RETR "
#define HELO "EHLO [127.0.0.1]\r\n"
#define DATA "DATA\r\n"
#define QUIT "QUIT\r\n"

int sock;
struct sockaddr_in server;

// Define the Email structure
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

    // Add MIME-Version header
    string mimeVersionHeader = "MIME-Version: 1.0\r\n";
    send_socket(mimeVersionHeader.c_str());

    // Add TO header
    string toHeader = "TO: ";
    toHeader += email.to[0]; // Assuming at least one recipient
    for (size_t i = 1; i < email.to.size(); i++) {
        toHeader += ", " + email.to[i];
    }
    toHeader += "\r\n";
    send_socket(toHeader.c_str());

    // Add FROM header
    string fromHeader = "FROM: <" + email.from + ">\r\n";
    send_socket(fromHeader.c_str());

    // Add CC header if present
    if (!email.cc.empty()) {
        string ccHeader = "CC: ";
        ccHeader += email.cc[0]; // Assuming at least one CC recipient
        for (size_t i = 1; i < email.cc.size(); i++) {
            ccHeader += ", " + email.cc[i];
        }
        ccHeader += "\r\n";
        send_socket(ccHeader.c_str());
    }

    // Add BCC header if present
    if (!email.bcc.empty()) {
        string bccHeader = "BCC: ";
        bccHeader += email.bcc[0]; // Assuming at least one BCC recipient
        for (size_t i = 1; i < email.bcc.size(); i++) {
            bccHeader += ", " + email.bcc[i];
        }
        bccHeader += "\r\n";
        send_socket(bccHeader.c_str());
    }

    // Add Subject header
    string subjectHeader = "Subject: " + email.subject + "\r\n";
    send_socket(subjectHeader.c_str());
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
    
    // Loop through each recipient in the "To" field
    for (const string& recipient : email.to) {
        send_socket("RCPT TO: "); // Mail to
        send_socket(recipient.c_str());
        send_socket("\r\n");
        read_socket(); // Recipient OK
    }
    
    // Loop through each recipient in the "To" field
    for (const string& recipient : email.cc) {
        send_socket("RCPT TO: "); // Mail to
        send_socket(recipient.c_str());
        send_socket("\r\n");
        read_socket(); // Recipient OK
    }

    // Loop through each recipient in the "To" field
    for (const string& recipient : email.bcc) {
        send_socket("RCPT TO: "); // Mail to
        send_socket(recipient.c_str());
        send_socket("\r\n");
        read_socket(); // Recipient OK
    }

    // Start email data transmission
    send_socket(DATA); // Body to follow
    send_email_headers(email); // Add email headers
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
    read_socket(); // Read reply

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
    char buf[BUFSIZ+1];
    int len = read(sock, buf, BUFSIZ);// Read reply
    buf[len] = '\0';
    string response(buf);

    // Check if login was successful
    if (response.find(".") != string::npos) {
    	cout << "choose email to read (number of index, 0 to exit): ";
	string index;
	cin >> index;
	if(index == "0"){
	    send_socket(QUIT);
	    read_socket();
	    close(sock);
	    return;
	}
	send_socket(RETR);
    	send_socket(index.c_str());
        send_socket("\r\n");
        read_socket(); // Recipient OK

    } 
    send_socket(QUIT); // Quit
    read_socket(); // Log off
    
    // Close socket
    close(sock);
}



// Hàm nhập thông tin email từ người dùng
Email inputEmailInfo() {
    Email email;
    string temp;

    cout << "This is email composition information: (Press ENTER to skip)\n";
    cout << "From: ";
    getline(cin, email.from, '\n');
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
    getline(cin, email.subject, '\n');

    cout << "Content: ";
    getline(cin, email.content, '\n');

    cout << "Attach files? (1. Yes, 2. No): ";
    int attachmentOption;
    cin >> attachmentOption;
    email.hasAttachment = (attachmentOption == 1);

    if (email.hasAttachment) {
        cout << "Number of files to attach: ";
        cin >> email.numAttachments;
        
        // Clear newline character from the input stream
        cin.ignore();
        
        for (int i = 0; i < email.numAttachments; i++) {
            cout << "Enter the path to file " << i + 1 << ": ";
            getline(cin, temp, '\n');
            email.files.push_back(temp);
            temp.clear();
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
    /*
    string index;
    cout << "Choose email to read: ";
    cin >> index;
    cout << index;
    readEmail(serverIP, pop3Port, username, password, index);
	*/
    // Đóng socket

    return 0;
}   

