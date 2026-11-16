// ============================================================================
//  WASI PAISA  -  Mobile Wallet (Console Application)
//  Compile:  g++ wasi_paisa.cpp -o wasi_paisa.exe
// ============================================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <iostream>
#include <string>
#include <vector>
#include <utility>
#include <iomanip>
#include <sstream>
#include <cctype>
#include <cstdlib>
#include <ctime>

#ifdef _WIN32
    #include <conio.h>
    #include <windows.h>
#else
    #include <termios.h>
    #include <unistd.h>
    static int getch() {
        struct termios oldt, newt;
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        int ch = getchar();
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        return ch;
    }
#endif

using namespace std;

// ----------------------------------------------------------------------------
//  Settings
// ----------------------------------------------------------------------------
const string APP_NAME        = "Wasi Paisa";
const int    MAX_TRANSACTION = 200000;   // maximum amount per transaction (Rs.)
const int    MAX_PIN_TRIES   = 3;
const string BACK_LABEL      = "Back to Main Menu";

#define CLR_RESET  "\033[0m"
#define CLR_BOLD   "\033[1m"
#define CLR_RED    "\033[31m"
#define CLR_GREEN  "\033[32m"
#define CLR_YELLOW "\033[33m"
#define CLR_CYAN   "\033[36m"
#define CLR_GRAY   "\033[90m"

// ----------------------------------------------------------------------------
//  Offer data (all bundle prices are kept in one place)
// ----------------------------------------------------------------------------
struct Offer {
    const char* desc;
    int price;
};

const char* PERIODS[4] = {"Monthly", "Weekly", "Daily", "Hourly"};
const char* TYPES[3]   = {"Data", "Call", "SMS"};

// OFFERS[period][type][offer]
const Offer OFFERS[4][3][3] = {
    { // Monthly
        { {"30 GB data", 700}, {"100 GB data", 2000}, {"200 GB data", 3600} },
        { {"3000 all-network minutes", 500}, {"6000 all-network minutes", 900}, {"10000 all-network minutes", 1300} },
        { {"3000 SMS", 120}, {"6000 SMS", 180}, {"10000 SMS", 250} }
    },
    { // Weekly
        { {"25 GB data", 340}, {"50 GB data", 450}, {"100 GB data", 540} },
        { {"1000 all-network minutes", 220}, {"500 all-network minutes", 130}, {"300 all-network minutes", 100} },
        { {"1000 SMS", 100}, {"500 SMS", 80}, {"300 SMS", 50} }
    },
    { // Daily
        { {"2 GB data", 25}, {"1500 MB data", 15}, {"1000 MB data", 10} },
        { {"100 all-network minutes", 80}, {"80 all-network minutes", 60}, {"50 all-network minutes", 50} },
        { {"100 SMS", 80}, {"80 SMS", 60}, {"60 SMS", 50} }
    },
    { // Hourly
        { {"2000 MB data (valid 2 hours)", 25}, {"1500 MB data (valid 1 hour)", 20}, {"1000 MB data (valid 1 hour)", 15} },
        { {"100 minutes (valid 2 hours)", 30}, {"80 minutes (valid 1 hour)", 25}, {"50 minutes (valid 1 hour)", 15} },
        { {"100 SMS (valid 2 hours)", 30}, {"80 SMS (valid 1 hour)", 25}, {"50 SMS (valid 1 hour)", 15} }
    }
};

const Offer SUPER_CARDS[3] = {
    {"Monthly: 100 GB internet, 3000 SMS, 5000 all-network minutes", 2000},
    {"Weekly: 30 GB internet, 1000 SMS, 2000 all-network minutes", 600},
    {"Daily: 2 GB internet, 100 SMS, 100 all-network minutes", 40}
};

// ----------------------------------------------------------------------------
//  Logged-in user
// ----------------------------------------------------------------------------
struct Session {
    string phone;
    string name;
    string pin;
};
Session user;

// ----------------------------------------------------------------------------
//  Console helpers
// ----------------------------------------------------------------------------
void enableColors() {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
        SetConsoleMode(h, mode | 0x0004); // enable ANSI colors
    }
#endif
}

void sleepMs(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

void clearScreen() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

void printHeader(const string& title) {
    cout << CLR_BOLD << CLR_CYAN
         << "\n  ==================================================\n"
         << "   " << title << "\n"
         << "  ==================================================" << CLR_RESET << "\n\n";
}

void beginScreen(const string& title) {
    clearScreen();
    printHeader(title);
}

void showSuccess(const string& m) { cout << CLR_GREEN  << "\n  [SUCCESS] " << m << CLR_RESET << endl; }
void showError  (const string& m) { cout << CLR_RED    << "\n  [ERROR]   " << m << CLR_RESET << endl; }
void showInfo   (const string& m) { cout << CLR_YELLOW << "\n  [INFO]    " << m << CLR_RESET << endl; }

void waitForKey() {
    cout << CLR_GRAY << "\n  Press any key to return to the main menu..." << CLR_RESET;
    getch();
    cout << endl;
}

void showProcessing() {
    clearScreen();
    cout << "\n\n  " << CLR_YELLOW << "Processing, please wait" << CLR_RESET;
    for (int i = 0; i < 4; i++) {
        cout << CLR_YELLOW << "." << CLR_RESET << flush;
        sleepMs(300);
    }
    cout << endl;
}

// "1500" -> "Rs. 1,500"
string rs(int v) {
    string s = to_string(v), out;
    int count = 0;
    for (int i = (int)s.size() - 1; i >= 0; i--) {
        out.insert(out.begin(), s[i]);
        if (++count % 3 == 0 && i > 0) out.insert(out.begin(), ',');
    }
    return "Rs. " + out;
}

string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ----------------------------------------------------------------------------
//  Input helpers (line based, so wrong input can never hang the program)
// ----------------------------------------------------------------------------
string readLine(const string& prompt) {
    string s;
    cout << prompt;
    if (!getline(cin, s)) {
        cout << endl;
        exit(0);
    }
    return trim(s);
}

string readMasked(const string& prompt) {
    string input;
    cout << prompt;
    while (true) {
        int ch = getch();
        if (ch == '\r' || ch == '\n') break;
        if (ch == 0 || ch == 0xE0) { getch(); continue; }
        if (ch == '\b' || ch == 127) {
            if (!input.empty()) { input.erase(input.size() - 1); cout << "\b \b"; }
        } else if (isprint(ch)) {
            input.push_back((char)ch);
            cout << '*';
        }
    }
    cout << endl;
    return input;
}

int readChoice(int maxOption, bool allowZero) {
    int minOption = allowZero ? 0 : 1;
    while (true) {
        string s = readLine("  Enter your choice: ");
        bool digitsOnly = !s.empty() && s.size() <= 3;
        for (size_t i = 0; i < s.size(); i++)
            if (!isdigit((unsigned char)s[i])) digitsOnly = false;
        if (digitsOnly) {
            int v = atoi(s.c_str());
            if (v >= minOption && v <= maxOption) return v;
        }
        showError("Invalid selection. Please enter a number from " + to_string(minOption) +
                  " to " + to_string(maxOption) + ".");
    }
}

// Shows a menu; returns 1..N, or 0 when the "back" option is chosen
int showMenu(const string& title, const vector<string>& options, const string& backLabel) {
    beginScreen(title);
    for (size_t i = 0; i < options.size(); i++)
        cout << "   " << CLR_YELLOW << (i + 1) << "." << CLR_RESET << " " << options[i] << "\n";
    if (!backLabel.empty())
        cout << "   " << CLR_YELLOW << "0." << CLR_RESET << " " << backLabel << "\n";
    cout << endl;
    return readChoice((int)options.size(), !backLabel.empty());
}

// Reads digits only (spaces and dashes are ignored)
string readDigits(const string& prompt, size_t minLen, size_t maxLen,
                  const string& errorMsg, bool mustStartWith03 = false) {
    while (true) {
        string raw = readLine(prompt), digits;
        bool bad = false;
        for (size_t i = 0; i < raw.size(); i++) {
            char c = raw[i];
            if (isdigit((unsigned char)c)) digits.push_back(c);
            else if (c != ' ' && c != '-') bad = true;
        }
        bool ok = !bad && digits.size() >= minLen && digits.size() <= maxLen;
        if (ok && mustStartWith03 && digits.substr(0, 2) != "03") ok = false;
        if (ok) return digits;
        showError(errorMsg);
    }
}

string readMobile(const string& prompt) {
    return readDigits(prompt, 11, 11,
        "Invalid mobile number. Enter 11 digits starting with 03 (e.g. 03001234567).", true);
}

string readName(const string& prompt) {
    while (true) {
        string n = readLine(prompt);
        bool ok = n.size() >= 3 && n.size() <= 30;
        bool hasLetter = false;
        for (size_t i = 0; i < n.size(); i++) {
            unsigned char c = (unsigned char)n[i];
            if (isalpha(c)) hasLetter = true;
            else if (c != ' ' && c != '.') ok = false;
        }
        if (ok && hasLetter) return n;
        showError("Invalid name. Use letters and spaces only (3 to 30 characters).");
    }
}

string readBankAccount(const string& prompt) {
    while (true) {
        string raw = readLine(prompt), acc;
        bool ok = true;
        for (size_t i = 0; i < raw.size(); i++) {
            unsigned char c = (unsigned char)raw[i];
            if (isalnum(c)) acc.push_back((char)toupper(c));
            else if (c != ' ' && c != '-') ok = false;
        }
        if (ok && acc.size() >= 8 && acc.size() <= 24) return acc;
        showError("Invalid account number. Enter 8 to 24 letters/digits (e.g. PK36SCBL0000001123456702).");
    }
}

int readAmount(const string& prompt, int minAmount, int maxAmount) {
    while (true) {
        string s = readLine(prompt);
        bool ok = !s.empty() && s.size() <= 9;
        for (size_t i = 0; i < s.size(); i++)
            if (!isdigit((unsigned char)s[i])) ok = false;
        if (ok) {
            int v = atoi(s.c_str());
            if (v >= minAmount && v <= maxAmount) return v;
        }
        showError("Invalid amount. Enter a whole number between " + rs(minAmount) +
                  " and " + rs(maxAmount) + ".");
    }
}

bool isValidPin(const string& p, size_t minLen, size_t maxLen) {
    return p.size() >= minLen && p.size() <= maxLen &&
           p.find_first_not_of("0123456789") == string::npos;
}

bool askYesNo(const string& prompt) {
    while (true) {
        string s = readLine(prompt + " (y/n): ");
        if (s == "y" || s == "Y" || s == "yes" || s == "YES") return true;
        if (s == "n" || s == "N" || s == "no"  || s == "NO")  return false;
        showError("Please type 'y' for yes or 'n' for no.");
    }
}

bool verifyPin() {
    for (int t = 1; t <= MAX_PIN_TRIES; t++) {
        string p = readMasked("  Enter your Wasi Paisa PIN to confirm: ");
        if (p == user.pin) return true;
        if (t < MAX_PIN_TRIES)
            showError("Incorrect PIN. Attempts remaining: " + to_string(MAX_PIN_TRIES - t));
    }
    showError("Too many incorrect attempts. The request has been cancelled for your security.");
    return false;
}

string maskCard(const string& card) {
    return "**** **** **** " + card.substr(card.size() - 4);
}

// ----------------------------------------------------------------------------
//  Transaction summary -> confirmation -> processing -> receipt
// ----------------------------------------------------------------------------
typedef vector<pair<string, string> > Details;

string newReferenceId() {
    static int seq = 0;
    seq++;
    ostringstream oss;
    oss << "WP" << (long)(time(NULL) % 100000000L) << setw(3) << setfill('0') << seq;
    return oss.str();
}

string currentDateTime() {
    time_t now = time(NULL);
    char buf[64];
    strftime(buf, sizeof(buf), "%d-%b-%Y  %I:%M %p", localtime(&now));
    return buf;
}

void printRows(const Details& rows) {
    for (size_t i = 0; i < rows.size(); i++)
        cout << "  " << left << setw(16) << rows[i].first << ": " << rows[i].second << "\n";
}

// amount <= 0 means "no amount to show"
void completeTransaction(const string& title, const Details& details, int amount,
                         const string& successMessage) {
    beginScreen("CONFIRM - " + title);
    printRows(details);
    if (amount > 0) cout << "  " << left << setw(16) << "Amount" << ": " << rs(amount) << "\n";
    cout << endl;

    if (!askYesNo("  Do you want to proceed?")) {
        showInfo("Cancelled. No action was taken and nothing was charged.");
        waitForKey();
        return;
    }

    showProcessing();
    beginScreen("SUCCESSFUL");
    showSuccess(successMessage);
    cout << CLR_BOLD << "\n  ------------------- RECEIPT -------------------" << CLR_RESET << "\n";
    cout << "  " << left << setw(16) << "Reference ID" << ": " << newReferenceId() << "\n";
    cout << "  " << left << setw(16) << "Date & Time"  << ": " << currentDateTime() << "\n";
    cout << "  " << left << setw(16) << "From"         << ": " << user.name << " (" << user.phone << ")\n";
    printRows(details);
    if (amount > 0) cout << "  " << left << setw(16) << "Amount" << ": " << rs(amount) << "\n";
    cout << "  -----------------------------------------------\n";
    waitForKey();
}

// Builds menu labels like "30 GB data ........ Rs. 700"
vector<string> offerLabels(const Offer* list, int count) {
    size_t width = 0;
    for (int i = 0; i < count; i++) {
        size_t len = string(list[i].desc).size();
        if (len > width) width = len;
    }
    vector<string> labels;
    for (int i = 0; i < count; i++) {
        ostringstream oss;
        oss << left << setw((int)width + 3) << list[i].desc << rs(list[i].price);
        labels.push_back(oss.str());
    }
    return labels;
}

// ----------------------------------------------------------------------------
//  Features
// ----------------------------------------------------------------------------
void login() {
    beginScreen("WELCOME TO " + string("WASI PAISA"));
    cout << "  Please log in to continue.\n\n";
    user.phone = readMobile("  Mobile number (e.g. 03001234567): ");
    user.name  = readName("  Your name: ");
    while (true) {
        string p = readMasked("  Enter your PIN (4 to 6 digits): ");
        if (isValidPin(p, 4, 6)) { user.pin = p; break; }
        showError("Invalid PIN. It must be 4 to 6 digits (numbers only).");
    }
    showProcessing();
    beginScreen("LOGIN");
    showSuccess("Login successful. Welcome, " + user.name + "!");
    sleepMs(1500);
}

void sendMoney() {
    int c = showMenu("SEND MONEY", {"Mobile Accounts", "Bank Accounts", "CNIC"}, BACK_LABEL);
    if (c == 0) return;

    if (c == 1) {
        int w = showMenu("SEND TO MOBILE ACCOUNT", {"Easy Paisa", "Jazz Cash", "Upaisa"}, BACK_LABEL);
        if (w == 0) return;
        static const char* wallets[] = {"", "Easy Paisa", "Jazz Cash", "UPaisa"};
        string wallet = wallets[w];

        beginScreen("SEND MONEY - " + wallet);
        string number = readMobile("  Recipient mobile number: ");
        string name   = readName("  Recipient name: ");
        int amount    = readAmount("  Amount (Rs.): ", 1, MAX_TRANSACTION);
        Details d = { {"Service", "Send Money - " + wallet}, {"Recipient", name}, {"Mobile No", number} };
        completeTransaction("SEND MONEY", d, amount,
            "Your " + wallet + " transfer to " + name + " was completed successfully.");
    } else if (c == 2) {
        beginScreen("SEND MONEY - BANK ACCOUNT");
        string name    = readName("  Account holder name: ");
        string account = readBankAccount("  Account number / IBAN: ");
        int amount     = readAmount("  Amount (Rs.): ", 1, MAX_TRANSACTION);
        Details d = { {"Service", "Send Money - Bank"}, {"Account Title", name}, {"Account No", account} };
        completeTransaction("SEND MONEY", d, amount,
            "Your bank transfer to " + name + " was completed successfully.");
    } else {
        beginScreen("SEND MONEY - CNIC");
        string cnic   = readDigits("  Recipient CNIC number (13 digits): ", 13, 13,
                                   "Invalid CNIC. Enter exactly 13 digits (e.g. 3520212345671).");
        string name   = readName("  Recipient name: ");
        int amount    = readAmount("  Amount (Rs.): ", 1, MAX_TRANSACTION);
        cout << endl;
        showInfo("For your security, please verify your PIN.");
        if (!verifyPin()) { waitForKey(); return; }
        Details d = { {"Service", "Send Money - CNIC"}, {"Recipient", name}, {"CNIC", cnic} };
        completeTransaction("SEND MONEY", d, amount,
            "Your CNIC payment to " + name + " was sent successfully.");
    }
}

void payBills() {
    int c = showMenu("PAY BILLS", {"Gas", "Electricity", "Water", "PTCL", "Internet"}, BACK_LABEL);
    if (c == 0) return;
    static const char* bills[] = {"", "Gas", "Electricity", "Water", "PTCL", "Internet"};
    string bill = bills[c];

    beginScreen("PAY BILLS - " + bill);
    string number = readDigits("  Consumer / reference number (digits only): ", 5, 20,
                               "Invalid bill number. Enter 5 to 20 digits.");
    int amount = readAmount("  Bill amount (Rs.): ", 1, MAX_TRANSACTION);
    Details d = { {"Service", "Bill Payment"}, {"Bill Type", bill}, {"Reference No", number} };
    completeTransaction("PAY " + bill + " BILL", d, amount,
        "Your " + bill + " bill has been paid successfully.");
}

void mobileLoad() {
    int c = showMenu("MOBILE LOAD", {"Mobile Load", "Advance Load"}, BACK_LABEL);
    if (c == 0) return;

    if (c == 1) {
        int n = showMenu("SELECT NETWORK", {"Telenor", "Zong", "Jazz", "Ufone"}, BACK_LABEL);
        if (n == 0) return;
        static const char* networks[] = {"", "Telenor", "Zong", "Jazz", "Ufone"};
        string network = networks[n];

        beginScreen(network + " LOAD");
        string number = readMobile("  Enter " + network + " mobile number: ");
        int amount    = readAmount("  Load amount (Rs. 100 to Rs. 3,000): ", 100, 3000);
        Details d = { {"Service", "Mobile Load"}, {"Network", network}, {"Mobile No", number} };
        completeTransaction(network + " LOAD", d, amount,
            "Your " + network + " load of " + rs(amount) + " was sent to " + number + " successfully.");
    } else {
        beginScreen("ADVANCE LOAD");
        string number = readMobile("  Enter mobile number for advance load: ");
        Details d = { {"Service", "Advance Load"}, {"Mobile No", number} };
        completeTransaction("ADVANCE LOAD", d, 30,
            "Rs. 30 advance load was sent to " + number + " successfully.");
    }
}

void bundles() {
    int c = showMenu("BUNDLES AND SUPER CARD", {"Packages", "All-in-One Super Card"}, BACK_LABEL);
    if (c == 0) return;

    string offerName;
    int price = 0;

    if (c == 1) {
        int p = showMenu("SELECT PACKAGE TYPE", {"Monthly", "Weekly", "Daily", "Hourly"}, BACK_LABEL);
        if (p == 0) return;
        int t = showMenu(string(PERIODS[p - 1]) + " OFFERS", {"Data", "Call", "SMS"}, BACK_LABEL);
        if (t == 0) return;
        const Offer* list = OFFERS[p - 1][t - 1];
        int o = showMenu(string(PERIODS[p - 1]) + " " + TYPES[t - 1] + " OFFERS",
                         offerLabels(list, 3), BACK_LABEL);
        if (o == 0) return;
        offerName = string(PERIODS[p - 1]) + " " + list[o - 1].desc;
        price = list[o - 1].price;
    } else {
        int s = showMenu("ALL-IN-ONE SUPER CARD", offerLabels(SUPER_CARDS, 3), BACK_LABEL);
        if (s == 0) return;
        offerName = string("Super Card - ") + SUPER_CARDS[s - 1].desc;
        price = SUPER_CARDS[s - 1].price;
    }

    beginScreen("ACTIVATE OFFER");
    string number = readMobile("  Mobile number to activate the offer on: ");
    Details d = { {"Service", "Bundle Activation"}, {"Offer", offerName}, {"Mobile No", number} };
    completeTransaction("ACTIVATE OFFER", d, price,
        "Your offer has been activated successfully on " + number + ".");
}

void payments() {
    beginScreen("PAYMENTS");
    string number = readMobile("  Mobile number to send the payment to: ");
    int amount    = readAmount("  Amount (Rs.): ", 1, MAX_TRANSACTION);
    Details d = { {"Service", "Payment"}, {"Mobile No", number} };
    completeTransaction("PAYMENT", d, amount,
        "Your payment was sent to " + number + " successfully.");
}

void requestMoney() {
    int c = showMenu("REQUEST MONEY", {"Mobile Account", "CNIC Account"}, BACK_LABEL);
    if (c == 0) return;

    if (c == 1) {
        beginScreen("REQUEST MONEY - MOBILE ACCOUNT");
        string number = readMobile("  Mobile number to request from: ");
        string name   = readName("  Name of the person: ");
        int amount    = readAmount("  Amount to request (Rs.): ", 1, MAX_TRANSACTION);
        Details d = { {"Service", "Money Request"}, {"Request From", name}, {"Mobile No", number} };
        completeTransaction("REQUEST MONEY", d, amount,
            "Your money request was sent to " + name + " successfully.");
    } else {
        beginScreen("REQUEST MONEY - CNIC ACCOUNT");
        string cnic = readDigits("  CNIC number (13 digits): ", 13, 13,
                                 "Invalid CNIC. Enter exactly 13 digits (e.g. 3520212345671).");
        string name = readName("  Name on CNIC: ");
        int amount  = readAmount("  Amount to request (Rs.): ", 1, MAX_TRANSACTION);
        Details d = { {"Service", "Money Request"}, {"Request From", name}, {"CNIC", cnic} };
        completeTransaction("REQUEST MONEY", d, amount,
            "Your money request was sent to " + name + " successfully.");
    }
}

void debitCard() {
    int c = showMenu("DEBIT CARD", {"Add Money from Debit Card", "Withdraw Money to Debit Card"}, BACK_LABEL);
    if (c == 0) return;

    beginScreen(c == 1 ? "ADD MONEY FROM DEBIT CARD" : "WITHDRAW TO DEBIT CARD");
    string card = readDigits("  Debit card number (16 digits): ", 16, 16,
                             "Invalid card number. Enter exactly 16 digits.");
    int amount = readAmount(c == 1 ? "  Amount to add (Rs.): " : "  Amount to withdraw (Rs.): ",
                            1, MAX_TRANSACTION);
    while (true) {
        string cardPin = readMasked("  Debit card PIN (4 digits): ");
        if (isValidPin(cardPin, 4, 4)) break;
        showError("Invalid card PIN. It must be exactly 4 digits.");
    }

    Details d = { {"Service", c == 1 ? "Add Money (Debit Card)" : "Withdraw (Debit Card)"},
                  {"Card No", maskCard(card)} };
    completeTransaction(c == 1 ? "ADD MONEY" : "WITHDRAW MONEY", d, amount,
        c == 1 ? rs(amount) + " was added from your debit card successfully."
               : rs(amount) + " was transferred to your debit card successfully.");
}

void myAccount() {
    beginScreen("MY ACCOUNT");
    showInfo("For your security, please verify your PIN to view account details.");
    if (!verifyPin()) { waitForKey(); return; }

    beginScreen("MY ACCOUNT");
    cout << "  " << left << setw(16) << "Account Name" << ": " << user.name << "\n"
         << "  " << left << setw(16) << "Mobile Number" << ": " << user.phone << "\n"
         << "  " << left << setw(16) << "PIN" << ": " << string(user.pin.size(), '*') << "\n"
         << "  " << left << setw(16) << "Status" << ": " << CLR_GREEN << "Active" << CLR_RESET << "\n";
    waitForKey();
}

void inviteFriend() {
    beginScreen("INVITE A FRIEND");
    cout << "  Invite a friend and earn money!\n\n"
         << "  Your invite code : " << CLR_BOLD << "WASI" << user.phone.substr(7) << CLR_RESET << "\n"
         << "  Share this code with your friends when they sign up for " << APP_NAME << ".\n";
    waitForKey();
}

// ----------------------------------------------------------------------------
//  Main
// ----------------------------------------------------------------------------
int main() {
    enableColors();
    login();

    while (true) {
        int choice = showMenu("WASI PAISA  -  MAIN MENU",
            {"Send Money", "Pay Bills", "Mobile Load", "Bundles and Super Card", "Payments",
             "Request Money", "Debit Card", "My Account", "Invite a Friend", "Exit"}, "");

        switch (choice) {
            case 1:  sendMoney();    break;
            case 2:  payBills();     break;
            case 3:  mobileLoad();   break;
            case 4:  bundles();      break;
            case 5:  payments();     break;
            case 6:  requestMoney(); break;
            case 7:  debitCard();    break;
            case 8:  myAccount();    break;
            case 9:  inviteFriend(); break;
            case 10:
                beginScreen("GOODBYE");
                cout << "  Thank you for using " << APP_NAME << ", " << user.name << ".\n"
                     << "  You have been logged out safely. Have a great day!\n\n";
                return 0;
        }
    }
}