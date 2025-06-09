# Academia Portal - Course Registration System


## Overview

Academia Portal is a multi-user course registration system designed to facilitate academic course management. The system provides a terminal-based interface for administrators, faculty members, and students to interact with a centralized database for course registration and management.

![Academia Portal](https://github.com/user-attachments/assets/f580e20c-34bf-4961-a141-a36fd9d26a7c)


## Features

### User Roles and Authentication
- **Administrator**: System management and user administration
- **Faculty**: Course management
- **Student**: Course enrollment and management
- Password-based authentication with role-specific access control

### Administrator Features
- Add new students with ID, name, and password
- Add new faculty members
- Toggle student status (active/inactive)
- Update existing user information

### Faculty Features
- Add new courses with ID, name, and maximum enrollment
- Remove existing courses
- View enrollment status of their courses
- Change password

### Student Features
- Enroll in available courses (with seat limit validation)
- Unenroll from courses
- View enrolled courses
- Change password

### System Features
- Concurrent multi-user support
- File-based persistent storage
- Proper file locking for data consistency
- Student account status management (active/inactive)

## Technical Details

- **Language**: C
- **Networking**: Socket programming (TCP/IP)
- **Concurrency**: Process-based parallelism using fork()
- **Data Storage**: Text files with proper locking mechanisms
- **Build System**: Make

## Data Structure

The system uses four text files for data persistence:

- students.txt: Student records (ID, name, password, status)
- faculty.txt: Faculty records (ID, name, password)
- courses.txt: Course information (ID, name, faculty ID, max seats)
- enrollments.txt: Student enrollment data (course ID, student IDs)

## Concurrency Handling

- Uses process-based concurrency (fork) to handle multiple clients
- Implements file locking with fcntl to prevent race conditions
- Simulates processing delays during course addition to demonstrate concurrency effects

## Installation and Usage

### Prerequisites
- Linux/UNIX-based system or WSL on Windows
- GCC compiler
- Make build system
- Telnet client

### Building from Source
```bash
git clone https://github.com/freakun0025/academia-portal.git
cd academia-portal
make
```

### Running the Server
```bash
./server
```

### Connecting as a Client
```bash
telnet localhost 9000
```

### Default Administrator Credentials
- Username: `admin`
- Password: `admin123`

## Project Structure

```
.
├── Makefile              # Build configuration
├── server.c              # Server implementation
├── data/                 # Data directory (created at runtime)
│   ├── students.txt      # Student records
│   ├── faculty.txt       # Faculty records
│   ├── courses.txt       # Course information
│   └── enrollments.txt   # Enrollment records
└── README.md             # Project documentation
```

## Demo

### Sample Workflow
1. Start the server with `./server`
2. Connect using `telnet localhost 9000`
3. Login as admin (admin/admin123)
4. Add faculty and students
5. Login as faculty to add courses
6. Login as student to enroll in courses



## Acknowledgements

This project was developed as a demonstration of system programming concepts including process management, file operations, network programming, and concurrency control.

## Future Enhancements

- Web interface
- Database integration
- Encryption for password storage
- Course prerequisites
- Registration period constraints
- Enhanced reporting capabilities
