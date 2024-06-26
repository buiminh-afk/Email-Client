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
#include "json.hpp"
#include <filesystem>
#include "timercpp.h"

using namespace std;
using json = nlohmann::json;
namespace fs = std::filesystem;

map<string, vector<string>> filters;
string serverIP = "";
string username = "";
string password = "";
int smtpPort;
int pop3Port;
int autoload;
int last_index_download;
int sock;
struct sockaddr_in server;

#define DATA "DATA\r\n"
#define HELO "HELO 192.168.56.1\r\n"
#define LIST "LIST\r\n"
#define QUIT "QUIT\r\n"
#define RETR "RETR "
#define STAT "STAT\r\n"
#define USER "USER your_username\r\n"
#define PASS "PASS your_password\r\n"
#define MaxSize 3000
// #define HELO "EHLO [127.0.0.1]\r\n"

//Update last id download
void updateLastIdDownload(const string& filename, int lastIdDownload) {
    // Read the JSON file
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Failed to open file: " << filename << endl;
        return;
    }

    json j;
    file >> j;
    file.close();

    // Update the last_id_download field
    j["last_id_download"] = lastIdDownload;

    // Write the updated JSON back to the file
    ofstream outfile(filename);
    if (!outfile.is_open()) {
        cerr << "Failed to open file for writing: " << filename << endl;
        return;
    }

    outfile << j.dump(4); // Pretty print JSON with 4 spaces indentation
    outfile.close();
}
// doc file config
void readConfigFromFile(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file >> std::ws, line))
    { // Skip leading white spaces
        size_t pos = line.find(": ");
        if (pos != std::string::npos)
        {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 2); // Skip ": " after the key
            if (key == "Username")
            {
                username = value;
            }
            else if (key == "Password")
            {
                password = value;
            }
            else if (key == "MailServer")
            {
                serverIP = value;
            }
            else if (key == "SMTP")
            {
                smtpPort = std::stoi(value);
            }
            else if (key == "POP3")
            {
                pop3Port = std::stoi(value);
            }
        }
    }

    file.close();
}

// Hàm để đọc cấu hình từ file JSON
void readConfigFromJSON(const string &filename)
{
    ifstream file(filename);
    if (!file.is_open())
    {
        cerr << "Failed to open file: " << filename << endl;
        return;
    }

    // Đọc dữ liệu từ file JSON vào một đối tượng json
    json j;
    file >> j;

    // Kiểm tra xem có trường "Username", "Password", "MailServer", "SMTP", "POP3", "Autoload" trong file JSON hay không
    username = j["Username"];
    password = j["Password"];
    serverIP = j["MailServer"];
    smtpPort = j["SMTP"];
    pop3Port = j["Pop3"]; // Sửa thành "Pop3"
    autoload = j["Autoload"];

    // Đọc trường "last_index_download" từ JSON và gán vào biến last_index_download
    if (j.find("last_id_download") != j.end())
    {
        last_index_download = j["last_id_download"];
    }

    // Đọc các bộ lọc từ JSON
    if (j.find("Filter") != j.end())
    {
        json filter = j["Filter"];
        for (json::iterator it = filter.begin(); it != filter.end(); ++it)
        {
            vector<string> keys;
            json keyJson = filter[it.key()]["key"];
            for (const auto &key : keyJson)
            {
                keys.push_back(key);
            }
            filters[it.key()] = keys;
        }
    }

    file.close();
}

// Hàm tính kích thước của file
long long getFileSize(const string &filename)
{
    // Mở file để đọc dưới dạng nhị phân
    ifstream file(filename, ios::binary);
    if (!file.is_open())
    {
        cerr << "Failed to open file: " << filename << endl;
        return -1;
    }

    // Di chuyển con trỏ tới cuối file để lấy kích thước
    file.seekg(0, ios::end);
    long long size = file.tellg();

    // Đóng file
    file.close();

    return size;
}

// Define the Email structure
struct Email
{
    vector<string> to;
    string from;
    vector<string> cc;
    vector<string> bcc;
    string subject;
    string content;
    bool hasAttachment = false;
    int numAttachments;
    vector<string> files;
};

// Hàm giải mã base64
string base64_decode(const string &in)
{
    string out;
    vector<int> T(256, -1);
    for (int i = 0; i < 64; i++)
    {
        T["ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i]] = i;
    }
    int val = 0, valb = -8;
    for (unsigned char c : in)
    {
        if (T[c] == -1)
            break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0)
        {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

string choose(map<string, vector<string>> filters, Email testmail) {
    string tag = "Inbox";
    cout << testmail.from;
    for (const auto &folder : filters) {
        for (const auto &key : folder.second) {
            if (testmail.from.find(key) != string::npos || testmail.subject.find(key) != string::npos || testmail.content.find(key) != string::npos) {
                tag = folder.first;
                return tag;
            }
        }
    }
    cout << "TAG: " << tag << endl;
    return tag;
}

// Function to send data to the socket
void send_socket(const char *s)
{
    write(sock, s, strlen(s));
    // write(1, s, strlen(s)); // Echo to console
}

// Function to read data from the socket
void read_socket()
{
    char buf[BUFSIZ + 1];
    int len = read(sock, buf, BUFSIZ);
    // write(1, buf, len); // Echo to console
}

void updateConfigFile(const string &configPath, int newIndex, const string &from, const string &subject)
{
    ofstream configFile(configPath, ios::app); // Mở file để ghi thêm vào cuối file
    if (configFile.is_open())
    {
        configFile << newIndex << " "
                   << "unread"
                   << " " << from << subject << endl; // Ghi thông tin mới vào file
        configFile.close();                           // Đóng file sau khi ghi
    }
    else
    {
        cerr << "Error updating config file: " << configPath << endl;
    }
}

void downEmail(const Email &email, const string &path)
{
    int currentIndex = 0;
    string configPath = path + "config.txt";
    ifstream configFile(configPath);
    if (configFile.is_open())
    {
        string line;
        while (getline(configFile, line))
        {
            istringstream iss(line);
            iss >> currentIndex; // Lấy index cuối cùng từ file
        }
        configFile.close(); // Đóng file config.txt
    }
    else
    {
        cerr << "Error opening config file: " << configPath << endl;
        return;
    }

    // Tăng index lên 1 để tạo index mới cho email
    int newIndex = currentIndex + 1;

    // Lưu thông tin email vào file config.txt
    updateConfigFile(configPath, newIndex, email.from, email.subject);

    // Lưu email dưới dạng index.txt
    string emailFilePath = path + "/" + to_string(newIndex) + ".txt";
    ofstream outputFile(emailFilePath);
    if (!outputFile.is_open())
    {
        cerr << "Unable to open file for writing." << endl;
        return;
    }

    // Ghi dữ liệu từ biến email vào file
    outputFile << "from: " << email.from << endl;
    outputFile << "to: ";
    for (const auto &recipient : email.to)
    {
        outputFile << recipient;
    }
    outputFile << endl;
    outputFile << "cc: ";
    for (const auto &cc : email.cc)
    {
        outputFile << cc;
    }
    outputFile << endl;
    outputFile << "bcc: ";
    for (const auto &bcc : email.bcc)
    {
        outputFile << bcc;
    }
    outputFile << endl;
    outputFile << "subject: " << email.subject;
    outputFile << endl;
    outputFile << "content: " << email.content;

    // Ghi thông tin về các file đính kèm
    outputFile << "attach: ";
    if (email.hasAttachment)
    {
        for (const auto &attachment : email.files)
        {
            size_t pos = attachment.find("\n"); // Tìm dấu xuống dòng đầu tiên để tách tên file và nội dung
            if (pos != string::npos)
            {
                string filename = attachment.substr(5, pos - 5); // Lấy tên file bo di "name "

                string content = attachment.substr(pos + 1); // Lấy nội dung từ sau dấu xuống dòng
                // Ghi tên file vào file văn bản
                outputFile << "Attach/" << filename << endl;
                // Ghi nội dung vào file tương ứng
                ofstream attachmentFile("Attach/" + filename); // Lưu file vào đường dẫn được chỉ định bởi path
                if (attachmentFile.is_open())
                {
                    attachmentFile << content;
                    attachmentFile.close();
                }
                else
                {
                    cerr << "Unable to open attachment file for writing: " << filename << endl;
                }
            }
        }
    }
    else
    {
        outputFile << "No attachments" << endl;
    }

    outputFile.close();
}

// Function to send email headers
void send_email_headers(const Email &email)
{
    // Add Date header
    time_t rawtime;
    struct tm *timeinfo;
    char buffer[80];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S %z", timeinfo);
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
    for (size_t i = 1; i < email.to.size(); i++)
    {
        toHeader += ", " + email.to[i];
    }
    toHeader += "\r\n";
    send_socket(toHeader.c_str());

    // Add FROM header
    string fromHeader = "FROM: <" + email.from + ">\r\n";
    send_socket(fromHeader.c_str());

    // Add CC header if present
    if (!email.cc.empty())
    {
        string ccHeader = "CC: ";
        ccHeader += email.cc[0]; // Assuming at least one CC recipient
        for (size_t i = 1; i < email.cc.size(); i++)
        {
            ccHeader += ", " + email.cc[i];
        }
        ccHeader += "\r\n";
        send_socket(ccHeader.c_str());
    }

    // Add BCC header if present
    if (!email.bcc.empty())
    {
        string bccHeader = "BCC: ";
        bccHeader += email.bcc[0]; // Assuming at least one BCC recipient
        for (size_t i = 1; i < email.bcc.size(); i++)
        {
            bccHeader += ", " + email.bcc[i];
        }
        bccHeader += "\r\n";
        send_socket(bccHeader.c_str());
    }

    // Add Subject header
    string subjectHeader = "Subject: " + email.subject + "\r\n";
    send_socket(subjectHeader.c_str());
}

bool sendEmailSMTP(const string &serverIP, int port, const Email &email)
{
    struct hostent *hp;
    string attachmentHeader = "Content-Type: application/octet-stream\r\n";
    attachmentHeader += "Content-Disposition: attachment; filename=\"";

    // Create Socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        perror("opening stream socket");
        exit(1);
    }
    else
    {
        // cout << "Socket created\n";
    }

    // Verify host
    server.sin_family = AF_INET;
    hp = gethostbyname(serverIP.c_str());
    if (hp == nullptr)
    {
        fprintf(stderr, "%s: unknown host\n", serverIP.c_str());
        exit(2);
    }

    // Connect to SMTP server
    memcpy((char *)&server.sin_addr, (char *)hp->h_addr, hp->h_length);
    server.sin_port = htons(port);
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) == -1)
    {
        perror("connecting stream socket");
        exit(1);
    }
    else
    {
        // cout << "Connected to SMTP server\n";
    }

    // SMTP communication
    read_socket();     // SMTP Server logon string
    send_socket(HELO); // Introduce ourselves
    read_socket();     // Read reply
    send_socket("MAIL FROM: ");
    send_socket(email.from.c_str());
    send_socket("\r\n");
    read_socket(); // Sender OK

    // Loop through each recipient in the "To" field
    for (const string &recipient : email.to)
    {
        send_socket("RCPT TO: "); // Mail to
        send_socket(recipient.c_str());
        send_socket("\r\n");
        read_socket(); // Recipient OK
    }

    // Loop through each recipient in the "To" field
    for (const string &recipient : email.cc)
    {
        send_socket("RCPT TO: "); // Mail to
        send_socket(recipient.c_str());
        send_socket("\r\n");
        read_socket(); // Recipient OK
    }

    // Loop through each recipient in the "To" field
    for (const string &recipient : email.bcc)
    {
        send_socket("RCPT TO: "); // Mail to
        send_socket(recipient.c_str());
        send_socket("\r\n");
        read_socket(); // Recipient OK
    }

    // Start email data transmission
    send_socket(DATA);         // Body to follow
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
    for (const string &attachment : email.files)
    {
        send_socket("--boundary-type-1234567892-alt\r\n");
        send_socket(attachmentHeader.c_str());
        send_socket(attachment.c_str());
        send_socket("\"\r\n");
        send_socket("Content-Transfer-Encoding: base64\r\n\r\n");

        // Read attachment content and encode to Base64
        ifstream file(attachment, ios::binary);
        if (file.is_open())
        {
            // Allocate a separate buffer for reading file content
            char buffer[1024];
            string encodedContent; // Store the entire encoded content

            // Read file content and encode to Base64
            while (file.read(buffer, sizeof(buffer)))
            {
                // Encode buffer content to Base64 and append to encodedContent
                string content(buffer, file.gcount());
                encodedContent += base64::to_base64(content);
                
            }

            // Encode remaining content and append to encodedContent
            if (file.gcount() > 0)
            {
                string content(buffer, file.gcount());
                encodedContent += base64::to_base64(content);
            }

            file.close();

            // Split encodedContent into smaller chunks and send sequentially
            const size_t chunkSize = 256; // Adjust as needed
            for (size_t i = 0; i < encodedContent.length(); i += chunkSize)
            {
                string chunk = encodedContent.substr(i, chunkSize) + "\r\n";
                send_socket(chunk.c_str());
            }
        }
        else
        {
            cerr << "Unable to open file: " << attachment << endl;
        }

        send_socket("\r\n");
    }

    send_socket("--boundary-type-1234567892-alt--\r\n");
    send_socket(".\r\n"); // End of data
    read_socket();        // Read reply

    send_socket(QUIT); // Quit
    read_socket();     // Log off

    return true;
}

Email parseEmail(const string &emailString)
{
    Email email;
    stringstream ss(emailString);
    string line;
    string boundary;

    while (getline(ss, line))
    {
        if (line.empty())
            continue;

        if (line.find("To: ") == 0 || line.find("TO: ") == 0)
        {
            email.hasAttachment = false;
            line.erase(remove(line.begin(), line.end(), '\r'), line.end());
            line.erase(remove(line.begin(), line.end(), '\n'), line.end());
            email.to.push_back(line.substr(4));
        }
        else if (line.find("From: ") == 0 || line.find("FROM: ") == 0)
        {
            line.erase(remove(line.begin(), line.end(), '\r'), line.end());
            line.erase(remove(line.begin(), line.end(), '\n'), line.end());
            size_t pos = line.find("<");
            string sender = line.substr(pos + 1, line.find(">", pos + 1) - pos - 1);
            email.from = sender;
        }
        else if (line.find("Cc: ") == 0 || line.find("CC: ") == 0)
        {
            line.erase(remove(line.begin(), line.end(), '\r'), line.end());
            line.erase(remove(line.begin(), line.end(), '\n'), line.end());
            email.cc.push_back(line.substr(4));
        }
        else if (line.find("Bcc: ") == 0 || line.find("BCC: ") == 0)
        {
            line.erase(remove(line.begin(), line.end(), '\r'), line.end());
            line.erase(remove(line.begin(), line.end(), '\n'), line.end());
            email.bcc.push_back(line.substr(5));
        }
        else if (line.find("Subject: ") == 0)
        {
            line.erase(remove(line.begin(), line.end(), '\r'), line.end());
            line.erase(remove(line.begin(), line.end(), '\n'), line.end());
            email.subject = line.substr(8);
        }
        else if (line.find("Content-Type: text/plain") != string::npos && line.find("name") == string::npos)
        {
            // Skip lines until reach the content part
            while (getline(ss, line) && line.find("Content-Transfer-Encoding:") == string::npos)
                ;
            // Skip one more line
            getline(ss, line);
            // Read content until boundary
            string content;
            while (getline(ss, line) && line.find("--") == string::npos)
            {
                content += line + "\n";
            }
            email.content = content;
        }
        else if (line.find("Content-Disposition: attachment") != string::npos)
        {

            // Xử lý phần đính kèm
            email.hasAttachment = true;
            // Skip lines until reach the filename part
            // Extract filename

            size_t pos = line.find("\"");
            string filename = line.substr(pos + 1, line.find("\"", pos + 1) - pos - 1);

            getline(ss, line);
            getline(ss, line);
            string filecontent;
            while (getline(ss, line) && line.find("--" + boundary) == string::npos)
                {
                    line.erase(remove(line.begin(), line.end(), '\r'), line.end());
                    line.erase(remove(line.begin(), line.end(), '\n'), line.end());
                    filecontent += line;
                }
            // if (filename.find(".pdf") != string::npos)
            // {
            //     while (getline(ss, line) && line.find("--" + boundary) == string::npos)
            //     {
            //         line.erase(remove(line.begin(), line.end(), '\r'), line.end());
            //         line.erase(remove(line.begin(), line.end(), '\n'), line.end());
            //         filecontent += line;
            //     }
            // }
            // else
            //     while (getline(ss, line) && line.find("--") == string::npos)
            //     {
            //         filecontent += line + "\n";
            //     }

            //  Decode base64 content
            string decoded_content = base64_decode(filecontent);

            // Save filename and content to vector
            email.files.push_back("name " + filename + "\n" + decoded_content);
        }
        else if (line.find("boundary=") != string::npos)
        {
            size_t pos = line.find("boundary=");
            boundary = line.substr(pos + 9);
        }
    }

    return email;
}

// Function to send login credentials to the server and check response
bool login(const string &username, const string &password)
{
    // Send username
    string userCommand = "USER " + username + "\r\n";
    send_socket(userCommand.c_str());
    read_socket(); // Read reply

    // Send password
    string passCommand = "PASS " + password + "\r\n";
    send_socket(passCommand.c_str());
    read_socket(); // Read reply

    // Read the response after sending password
    char buf[BUFSIZ + 1];
    int len = read(sock, buf, BUFSIZ);
    buf[len] = '\0';
    string response(buf);

    // Check if login was successful
    if (response.find("+OK") != string::npos)
    {
        // cout << "Login successful.\n";
        return true;
    }
    else
    {
        // cout << "Login failed.\n";
        return false;
    }
}

// Hàm để lấy số lượng email từ phản hồi STAT
int getEmailCount(const std::string &response)
{
    std::istringstream iss(response);
    std::string status, emailCountStr;

    // Đọc phần status từ phản hồi
    iss >> status;

    // Kiểm tra xem phản hồi có bắt đầu bằng "+OK" không
    if (status == "+OK")
    {
        // Đọc số lượng email từ phản hồi
        iss >> emailCountStr;
        try
        {
            // Chuyển đổi số lượng email từ chuỗi sang số nguyên
            int emailCount = std::stoi(emailCountStr);
            return emailCount;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Failed to parse email count: " << e.what() << std::endl;
            return -1; // Trả về -1 nếu có lỗi khi chuyển đổi
        }
    }
    else
    {
        std::cerr << "Unexpected response status: " << status << std::endl;
        return -1; // Trả về -1 nếu status không bắt đầu bằng "+OK"
    }
}

// Function to receive email from POP3 server
void autoDownload(const string &serverIP, int port, const string &username, const string &password)
{
    struct hostent *hp;

    // Create Socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        perror("opening stream socket");
        exit(1);
    }
    else
    {
        // std::cout << "Socket created\n";
    }

    // Verify host
    server.sin_family = AF_INET;
    hp = gethostbyname(serverIP.c_str());
    if (hp == nullptr)
    {
        fprintf(stderr, "%s: unknown host\n", serverIP.c_str());
        exit(2);
    }

    // Connect to POP3 server
    memcpy((char *)&server.sin_addr, (char *)hp->h_addr, hp->h_length);
    server.sin_port = htons(port);
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) == -1)
    {
        perror("connecting stream socket");
        exit(1);
    }
    else
    {
        // std::cout << "Connected to POP3 server\n";
    }

    if (login(username, password))
    {

        // List emails
        send_socket(STAT);
        char buf[BUFSIZ + 1];
        int len = read(sock, buf, BUFSIZ); // Read reply
        buf[len] = '\0';
        string response(buf);
        Email result;
        int numberOfEmail = getEmailCount(response);
        int i;
        if (numberOfEmail > 0)
        {
            for (i = last_index_download+1; i <= numberOfEmail; i++)
            {
                string temp = "";
                string tmp = "RETR " + to_string(i) + "\r\n";
                send_socket(tmp.c_str());
                while (temp.find("--\r\n.\r\n") == string::npos)
                {
                    char buff[BUFSIZ + 1];
                    int leng = read(sock, buff, BUFSIZ);
                    temp += buff;
                }

                if (temp.find("--\r\n.\r\n") != string::npos)
                {
                    result = parseEmail(temp);
                    string tag = choose(filters, result);
                    // cout << tag << " haha " <<endl;
                    tag += "/";
                    downEmail(result, tag);
                }
            }
            last_index_download = numberOfEmail;
        }
    }
    send_socket(QUIT); // Quit
    read_socket();     // Log off

    // Close socket
    close(sock);
}

// Hàm nhập thông tin email từ người dùng
Email inputEmailInfo()
{
    Email email;
    string temp;

    cout << "This is email composition information: (Press ENTER to skip)\n";
    cout << "From: ";
    getline(cin, email.from, '\n');
    cout << "To: ";
    getline(cin, temp, '\n');
    if (!temp.empty())
    {
        stringstream toStream(temp);
        while (getline(toStream, temp, ','))
        {
            email.to.push_back(temp);
        }
    }

    cout << "CC: ";
    getline(cin, temp, '\n');
    if (!temp.empty())
    {
        stringstream toStream(temp);
        while (getline(toStream, temp, ','))
        {
            email.cc.push_back(temp);
        }
    }

    cout << "BCC: ";
    getline(cin, temp, '\n');
    if (!temp.empty())
    {
        stringstream toStream(temp);
        while (getline(toStream, temp, ','))
        {
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

    if (email.hasAttachment)
    {
        cout << "Number of files to attach: ";
        cin >> email.numAttachments;

        // Clear newline character from the input stream
        cin.ignore();

        for (int i = 0; i < email.numAttachments; i++)
        {
            cout << "Enter the path to file " << i + 1 << ": ";
            getline(cin, temp, '\n');
            while (getFileSize(temp) > MaxSize) {
                temp.clear();
                cout << "File must be < 3mb\nEnter again: ";
                getline(cin, temp, '\n');
            }
            email.files.push_back(temp);
            temp.clear();
        }
    }
    return email;
}

struct EmailInfo
{
    int index;
    string sender;
    string subject;
    string readed;
};

vector<EmailInfo> listEmail(const string &path)
{
    vector<EmailInfo> emails;

    string configFilePath = path + "/config.txt";
    ifstream configFile(configFilePath);
    if (!configFile.is_open())
    {
        cerr << "Error opening config file: " << configFilePath << endl;
        return emails;
    }

    string line;
    while (getline(configFile, line))
    {
        EmailInfo email;
        istringstream iss(line);
        if (!(iss >> email.index >> email.readed >> email.sender))
        {
            cerr << "Error parsing config file: " << configFilePath << endl;
            break;
        }
        int pos = line.find(".com");
        email.subject = line.substr(pos + 4);

        emails.push_back(email);
    }

    configFile.close();

    return emails;
}

// Hàm để liệt kê tất cả các tệp trong một thư mục
void listFilesInDirectory(const std::string &directoryPath)
{
    vector<EmailInfo> emails = listEmail(directoryPath);

    // In thông tin các email ra màn hình
    for (const auto &email : emails)
    {
        cout << "Index: " << email.index << endl;
        if (email.readed == "unread")
            cout << "Unread" << endl;
        cout << "Sender: " << email.sender << endl;
        cout << "Subject: " << email.subject << endl;
        cout << "-------------------" << endl;
    }
}

void changeUnread(int index, string &path)
{
    index--;
    ifstream inFile(path);
    vector<string> lines;
    string line;

    // Đọc tất cả các dòng từ tệp vào vector
    while (getline(inFile, line))
    {
        lines.push_back(line);
    }

    // Kiểm tra xem index có hợp lệ không
    if (index < 0 || index >= lines.size())
    {
        // cout << "Index out of range." << endl;
        return;
    }

    // Thay đổi từ "unread" thành "read" trên dòng có index tương ứng
    size_t found = lines[index].find("unread");
    // cout << found << " pos unread " << endl;
    if (found != string::npos)
    {
        lines[index].replace(found, 6, "read"); // 6 là độ dài của từ "unread"
    }

    inFile.close();

    // Ghi lại tất cả các dòng vào tệp
    ofstream outFile(path);
    if (outFile.is_open())
    {
        for (const string &modifiedLine : lines)
        {
            outFile << modifiedLine << endl;
        }
        // cout << "File updated successfully." << endl;
    }
    else
    {
        cerr << "Error opening file for writing." << endl;
    }
}

void readEmail(const string &folderPath, const int emailFileName)
{

    string fileName = folderPath + "/" + to_string(emailFileName) + ".txt";
    string configFile = folderPath + "/config.txt";
    changeUnread(emailFileName, configFile);
    ifstream inputFile(fileName);
    if (!inputFile.is_open())
    {
        cerr << "Error opening file: " << fileName << endl;
        return;
    }

    // Biến lưu thông tin email
    Email email;

    // Biến kiểm tra xem đã đọc phần content hay chưa
    bool contentRead = false;

    // Đọc từng dòng của file email
    string line, line2;
    while (getline(inputFile, line))
    {

        if (line.find("attach: ") != string::npos)
        {
            string attachPath = line.substr(8); // Lấy phần sau "attach: "
            email.files.push_back(attachPath);
            while (getline(inputFile, line2))
            {
                email.files.push_back(line2);
            }
        }
        else
            cout << line << endl;
    }

    inputFile.close();
    // In nội dung các file đính kèm (nếu có)
    for (const string &attachPath : email.files)
    {
        // string fullPath = folderPath + "/" + attachPath;
        cout << "Attachment: " << attachPath << endl;
        ifstream attachFile(attachPath);
        if (attachFile.is_open())
        {
            while (getline(attachFile, line))
            {
                cout << line << endl;
            }
            attachFile.close();
        }
        else
        {
            cerr << "Error opening attachment file: " << attachPath << endl;
        }
    }
}

int main()
{
    // Doc file config JSON
    readConfigFromJSON("filter.json");
    // Set up auto download
    Timer t = Timer();
    t.setInterval([&]()
                  { autoDownload(serverIP, pop3Port, username, password); },
                  autoload);
    int choice = 0;
    while (choice != 3)
    {
        cout << "Menu" << endl;
        cout << "1. Send Email" << endl;
        cout << "2. See the Mailbox" << endl;
        cout << "3. Exit" << endl;
        cout << "You choose: ";
        cin >> choice;
        while (choice < 1 || choice > 3)
        {
            cout << "Please enter again: ";
            cin >> choice;
        }
        switch (choice)
        {
        case 1:
        {
            cin.ignore();
            // Nhập thông tin email từ người dùng
            Email email = inputEmailInfo();
            // Gửi email
            bool sent = sendEmailSMTP(serverIP, smtpPort, email);
            if (sent)
            {
                cout << "Email sent successfully.\n";
            }
            else
            {
                cout << "Failed to send email.\n";
            }
            close(sock);
            break;
        }
        case 2:
        {
            cout << "Enter the folder to read the email: (Inbox, Important, Work, Project, Spam) ";
            string folderPath;
            cin >> folderPath;

            // Liệt kê các email trong thư mục
            cout << "List of Emails in Folder " << folderPath << ":" << endl;
            listFilesInDirectory(folderPath);

            // Chọn email để đọc
            int emailIndex;
            cout << "Enter the index of the email you want to read: ";
            cin >> emailIndex;

            // Đọc và in email
            cout << "Reading Email " << emailIndex << ":" << endl;
            readEmail(folderPath, emailIndex);
            break;
        }
        case 3:
            updateLastIdDownload("filter.json", last_index_download);
            break;
        }
    }
    return 0;
}