// Course Registration Portal (Academia) Mini Project
// Kunal Mittal (IMT2023533)

#include <stdio.h>      // only for snprintf, perror
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <errno.h>

// Add this include for sleep function
#include <time.h>

#define PORT      9000
#define BACKLOG   10
#define BUF_SIZE 1024
// Add a sleep duration in seconds
#define COURSE_ADD_DELAY 20

const char *STUD_FILE = "data/students.txt";
const char *FAC_FILE  = "data/faculty.txt";
const char *CRS_FILE  = "data/courses.txt";
const char *ENR_FILE  = "data/enrollments.txt";

// Acquire a blocking fcntl lock
int lock_fd(int fd, short type) {
    struct flock fl = { .l_type = type, .l_whence = SEEK_SET };
    return fcntl(fd, F_SETLKW, &fl);
}

// Send a C-string to the client
void send_str(int cfd, const char *s) {
    write(cfd, s, strlen(s));
}

// Read one line (including '\n') from client
ssize_t read_line(int cfd, char *buf, size_t max) {
    ssize_t n = 0; char ch;
    while (n + 1 < (ssize_t)max && read(cfd, &ch, 1) == 1) {
        buf[n++] = ch;
        if (ch == '\n') break;
    }
    buf[n] = '\0';
    return n;
}

// Trim leading/trailing whitespace/newlines
void trim(char *s) {
    int start = 0, end = strlen(s)-1;
    while (start<=end && (s[start]==' '||s[start]=='\r'||s[start]=='\n')) start++;
    while (end>=start && (s[end]==' '||s[end]=='\r'||s[end]=='\n')) s[end--]='\0';
    if (start) memmove(s, s+start, end-start+2);
}

// 1) Append a line under exclusive lock (syscalls only)
void append_line_sys(const char *file, const char *line) {
    int fd = open(file, O_WRONLY|O_APPEND);
    if (fd<0) return;
    lock_fd(fd, F_WRLCK);
    write(fd, line, strlen(line));
    write(fd, "\n", 1);
    close(fd);
}

// 2) Replace or delete a single line matching prefix: (syscalls only)
void rewrite_single_line_sys(const char *file,
                             const char *prefix,
                             const char *newline,
                             int cfd) {
    int fd_in = open(file, O_RDONLY);
    if (fd_in<0) { send_str(cfd,"Open error\n"); return; }

    char tmp[] = "/tmp/rwXXXXXX";
    int fd_tmp = mkstemp(tmp);
    if (fd_tmp < 0) {
        perror("mkstemp failed");
        close(fd_in);  // Fix 1: Changed 'in' to 'fd_in'
        send_str(cfd, "Error creating temporary file.\n");
        return;  // Fix 2: Changed 'continue' to 'return' since we're not in a loop
    }

    lock_fd(fd_in,  F_RDLCK);
    lock_fd(fd_tmp, F_WRLCK);

    char match[BUF_SIZE];
    size_t mlen = snprintf(match, sizeof(match), "%s:", prefix);

    char buf[BUF_SIZE];
    size_t len = 0;
    ssize_t r;
    int found = 0;

    while ((r = read(fd_in, buf+len, 1)) == 1) {
        len++;
        if (buf[len-1]=='\n' || len==BUF_SIZE-1) {
            if (len>=mlen && !memcmp(buf, match, mlen)) {
                found = 1;
                if (newline[0]) {
                    write(fd_tmp, newline, strlen(newline));
                    write(fd_tmp, "\n", 1);
                }
            } else {
                write(fd_tmp, buf, len);
            }
            len = 0;
        }
    }
    if (len) {  // leftover
        if (len>=mlen && !memcmp(buf, match, mlen)) {
            found = 1;
            if (newline[0]) {
                write(fd_tmp, newline, strlen(newline));
                write(fd_tmp, "\n", 1);
            }
        } else {
            write(fd_tmp, buf, len);
        }
    }

    close(fd_in);
    close(fd_tmp);

    if (!found) {
        send_str(cfd,"Not found\n");
        unlink(tmp);
        return;
    }
    rename(tmp, file);
}

// 3) Count students in course cid (syscalls only)
int count_enroll_sys(const char *cid) {
    int fd = open(ENR_FILE, O_RDONLY);
    if (fd<0) return 0;
    lock_fd(fd, F_RDLCK);

    char prefix[BUF_SIZE];
    size_t pfx = snprintf(prefix, sizeof(prefix), "%s:", cid);

    char buf[BUF_SIZE];
    size_t len = 0;
    ssize_t r;
    int count = 0;

    while ((r = read(fd, buf+len, 1)) == 1) {
        len++;
        if (buf[len-1]=='\n' || len==BUF_SIZE-1) {
            if (len>= (ssize_t)pfx && !memcmp(buf, prefix, pfx)) {
                count = 1;
                for (size_t i = pfx; i < len; i++)
                    if (buf[i]==',') count++;
                break;
            }
            len = 0;
        }
    }

    close(fd);
    return count;
}

// 4) Read each line from file, calling process(buf,len,userdata)
int read_file_lines_sys(const char *file,
                        void (*process)(const char*, size_t, void*),
                        void *userdata) {
    int fd = open(file, O_RDONLY);
    if (fd<0) return -1;
    lock_fd(fd, F_RDLCK);

    char buf[BUF_SIZE];
    size_t len = 0;
    ssize_t r;
    while ((r = read(fd, buf+len, 1)) == 1) {
        len++;
        if (buf[len-1]=='\n' || len==BUF_SIZE-1) {
            process(buf, len, userdata);
            len = 0;
        }
    }
    if (len) process(buf, len, userdata);

    close(fd);
    return 0;
}



int authenticate(const char *file, const char *name, const char *pwd, int check_active) {
    FILE *f = fopen(file,"r");
    if(!f) return 0;
    char line[BUF_SIZE], *fld[4];
    int ok = 0;
    while (fgets(line,sizeof(line),f)) {
        trim(line);
        if (!*line) continue;
        char *p = line; int i=0;
        while (i<4 && p) { fld[i++] = p; p=strchr(p,':'); if (p)*p++ = '\0'; }
        if (fld[1] && fld[2] && strcmp(fld[1],name)==0 && strcmp(fld[2],pwd)==0) {
            if (!check_active) ok = 1;
            else if (fld[3] && strcmp(fld[3],"active")==0) ok = 1;
        }
        if (ok) break;
    }
    fclose(f);
    return ok;
}

int find_id_by_name(const char *file,const char *name,char *id_out){
    FILE *f = fopen(file,"r");
    if(!f) return 0;
    char line[BUF_SIZE], *fld[4];
    while(fgets(line,sizeof(line),f)) {
        trim(line);
        if (!*line) continue;
        char *p=line; int i=0;
        while (i<4 && p) { fld[i++] = p; p=strchr(p,':'); if(p)*p++ = '\0'; }
        if (fld[1] && strcmp(fld[1],name)==0) {
            strcpy(id_out, fld[0]);
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

void toggle_student_status(const char *sid,int cfd){

    FILE *f = fopen(STUD_FILE,"r");
    if(!f){ send_str(cfd,"Error\n"); return; }
    char line[BUF_SIZE], prefix[BUF_SIZE];
    snprintf(prefix,sizeof(prefix),"%s:",sid);
    int found=0;
    while (fgets(line,sizeof(line),f)) {
        if (!strncmp(line,prefix,strlen(prefix))) { found=1; break; }
    }
    fclose(f);
    if(!found){ send_str(cfd,"Not found\n"); return; }
    trim(line);
    char *fld[4], *p=line; int i=0;
    while(i<4 && p){ fld[i++] = p; p=strchr(p,':'); if(p)*p++ = '\0'; }
    char *newst = strcmp(fld[3],"active")==0?"inactive":"active";
    char rec[BUF_SIZE];
    snprintf(rec,sizeof(rec),"%s:%s:%s:%s",fld[0],fld[1],fld[2],newst);
    rewrite_single_line_sys(STUD_FILE, fld[0], rec, cfd);
    send_str(cfd,"Toggled.\n");
}


void handle_client(int cfd) {
    char buf[BUF_SIZE], name[50], id[50], pwd[50];

    while (1) {
        // Main menu
        send_str(cfd,
          "=== Academia Portal ===\n"
          "1)Admin 2)Faculty 3)Student 4)Exit\n"
          "Choice: ");
        read_line(cfd,buf,sizeof(buf)); trim(buf);
        int role = atoi(buf);
        if (role<1||role>4) { send_str(cfd,"Invalid\n"); continue; }
        if (role==4) break;

        // Login loop
        int auth=0;
        while (!auth) {
            send_str(cfd,"Name: ");
            read_line(cfd,buf,sizeof(buf)); trim(buf); strcpy(name,buf);
            send_str(cfd,"Password: ");
            read_line(cfd,buf,sizeof(buf)); trim(buf); strcpy(pwd,buf);

            if (role==1) {
                auth = (!strcmp(name,"admin") && !strcmp(pwd,"admin123"));
                if (auth) strcpy(id,"admin");
            } else {
                auth = authenticate(role==2?FAC_FILE:STUD_FILE, name,pwd, role==3);
                if (auth) find_id_by_name(role==2?FAC_FILE:STUD_FILE,name,id);
            }
            if (!auth) send_str(cfd,"Auth failed.\n");
        }
        send_str(cfd,"Login successful.\n");

        // Role-based menu
        while (1) {
            if (role==1) {
                send_str(cfd,
                  "[Admin]\n"
                  "1)AddStu 2)AddFac 3)ToggleStu 4)UpdUser 5)Logout\n"
                  "Choice: ");
                read_line(cfd,buf,sizeof(buf)); trim(buf);
                if (buf[0]=='5') break;
                if (buf[0]=='1') {
                    send_str(cfd,"sid,name,pwd: ");
                    read_line(cfd,buf,sizeof(buf)); trim(buf);
                    char *s=strtok(buf,","),*n=strtok(NULL,","),*p=strtok(NULL,",");
                    char rec[BUF_SIZE];
                    snprintf(rec,sizeof(rec),"%s:%s:%s:active",s,n,p);
                    append_line_sys(STUD_FILE, rec);
                    send_str(cfd,"Student added.\n");
                }
                else if (buf[0]=='2') {
                    send_str(cfd,"fid,name,pwd: ");
                    read_line(cfd,buf,sizeof(buf)); trim(buf);
                    char *f_=strtok(buf,","),*n=strtok(NULL,","),*p=strtok(NULL,",");
                    char rec[BUF_SIZE];
                    snprintf(rec,sizeof(rec),"%s:%s:%s",f_,n,p);
                    append_line_sys(FAC_FILE, rec);
                    send_str(cfd,"Faculty added.\n");
                }
                else if (buf[0]=='3') {
                    send_str(cfd,"sid to toggle: ");
                    read_line(cfd,buf,sizeof(buf)); trim(buf);
                    toggle_student_status(buf,cfd);
                }
                else if (buf[0]=='4') {
                    send_str(cfd,"type(student/faculty),id,name,pwd: ");
                    read_line(cfd,buf,sizeof(buf)); trim(buf);
                    char *t=strtok(buf,","),*u=strtok(NULL,","),*n=strtok(NULL,","),*p=strtok(NULL,",");
                    char rec[BUF_SIZE];
                    if (!strcmp(t,"student"))
                        snprintf(rec,sizeof(rec),"%s:%s:%s:active",u,n,p);
                    else
                        snprintf(rec,sizeof(rec),"%s:%s:%s",u,n,p);
                    rewrite_single_line_sys(!strcmp(t,"student")?STUD_FILE:FAC_FILE, u, rec, cfd);
                    send_str(cfd,"User updated.\n");
                }
                else send_str(cfd,"Invalid\n");
            }

            else if (role==2) {
                send_str(cfd,
                  "[Faculty]\n"
                  "1)AddCourse 2)RemCourse 3)ViewEnroll 4)ChPwd 5)Logout\n"
                  "Choice: ");
                read_line(cfd,buf,sizeof(buf)); trim(buf);
                if (buf[0]=='5') break;
                if (buf[0]=='1') {
                    send_str(cfd,"cid,name,maxSeats: ");
                    read_line(cfd,buf,sizeof(buf)); trim(buf);
                    char *c=strtok(buf,","),*n=strtok(NULL,","),*m=strtok(NULL,",");
                    
                    // Inform the faculty that they need to wait
                    char wait_msg[BUF_SIZE];
                    snprintf(wait_msg, sizeof(wait_msg), 
                             "Processing course addition. Please wait %d seconds...\n", 
                             COURSE_ADD_DELAY);
                    send_str(cfd, wait_msg);
                    
                    // Add sleep to simulate database or system processing time
                    sleep(COURSE_ADD_DELAY);
                    
                    char rec[BUF_SIZE];
                    snprintf(rec,sizeof(rec),"%s:%s:%s:%s",c,n,id,m);
                    append_line_sys(CRS_FILE, rec);
                    send_str(cfd,"Course added.\n");
                }
                else if (buf[0]=='2') {
                    send_str(cfd,"cid to remove: ");
                    read_line(cfd,buf,sizeof(buf)); trim(buf);
                    rewrite_single_line_sys(CRS_FILE, buf, "", cfd);
                    send_str(cfd,"Course removed.\n");
                }
                else if (buf[0]=='3') {
                    send_str(cfd,"Your courses and enrollments:\n");
                    // for each course where fld[2]==id, print name,id: count, list
                    FILE *cf = fopen(CRS_FILE,"r");
                    char line[BUF_SIZE];
                    while (cf && fgets(line,sizeof(line),cf)) {
                        trim(line);
                        char *fld[4], *p=line; int i=0;
                        while (i<4 && p) { fld[i++]=p; p=strchr(p,':'); if(p)*p++='\0'; }
                        if (strcmp(fld[2], id)) continue;
                        int cnt = count_enroll_sys(fld[0]);
                        // pull list
                        char students[BUF_SIZE] = "";
                        int ef = open(ENR_FILE, O_RDONLY);
                        lock_fd(ef, F_RDLCK);
                        char buf2[BUF_SIZE];
                        size_t len2=0; ssize_t r2;
                        char prefix2[BUF_SIZE];
                        size_t p2 = snprintf(prefix2,sizeof(prefix2),"%s:",fld[0]);
                        while ((r2=read(ef, buf2+len2,1))==1) {
                            len2++;
                            if (buf2[len2-1]=='\n' || len2==BUF_SIZE-1) {
                                if (len2>=p2 && !memcmp(buf2, prefix2, p2)) {
                                    // copy after colon
                                    size_t st = p2;
                                    while (st<len2 && (buf2[st]=='\r'||buf2[st]=='\n')) st++;
                                    memcpy(students, buf2+p2, len2-p2);
                                    students[len2-p2] = '\0';
                                }
                                len2=0;
                            }
                        }
                        close(ef);

                        char out[BUF_SIZE];
                        if (cnt>0)
                            snprintf(out,sizeof(out), "%s,%s: %d, %s\n",
                                     fld[1], fld[0], cnt, students);
                        else
                            snprintf(out,sizeof(out), "%s,%s: 0\n",
                                     fld[1], fld[0]);
                        send_str(cfd,out);
                    }
                    if (cf) fclose(cf);
                }
                else if (buf[0]=='4') {
                    send_str(cfd,"new pwd: ");
                    read_line(cfd,buf,sizeof(buf)); trim(buf);
                    char rec[BUF_SIZE];
                    snprintf(rec,sizeof(rec),"%s::%s",id,buf);
                    rewrite_single_line_sys(FAC_FILE,id,rec,cfd);
                    send_str(cfd,"Password changed.\n");
                }
                else send_str(cfd,"Invalid\n");
            }

            else {  // Student menu
                send_str(cfd,
                  "[Student]\n"
                  "1)Enroll 2)Unenroll 3)View 4)ChPwd 5)Logout\n"
                  "Choice: ");
                read_line(cfd,buf,sizeof(buf)); trim(buf);
                if (buf[0]=='5') break;

                if (buf[0]=='1') {
                    send_str(cfd,"Enter courseID to enroll: ");
                    read_line(cfd,buf,sizeof(buf)); trim(buf);
                    char *cid = buf;
                    
                    // Check if course exists
                    FILE *cf = fopen(CRS_FILE, "r");
                    char cline[BUF_SIZE];
                    int course_found = 0;
                    int max_seats = 0;
                    
                    while (cf && fgets(cline, sizeof(cline), cf)) {
                        trim(cline);
                        char *fld[4], *p = cline; 
                        int i = 0;
                        while (i < 4 && p) { 
                            fld[i++] = p; 
                            p = strchr(p, ':'); 
                            if(p) *p++ = '\0'; 
                        }
                        
                        if (strcmp(fld[0], cid) == 0) {
                            course_found = 1;
                            max_seats = atoi(fld[3]);
                            break;
                        }
                    }
                    if (cf) fclose(cf);
                    
                    if (!course_found) {
                        send_str(cfd, "Course not found.\n");
                        continue;
                    }
                    
                    // Check enrollment count
                    int current_enrollment = count_enroll_sys(cid);
                    if (current_enrollment >= max_seats) {
                        send_str(cfd, "Course is full.\n");
                        continue;
                    }
                    
                    // rewrite ENR_FILE
                    int in = open(ENR_FILE,O_RDONLY);
                    char tmp[]="./tmp_enrXXXXXX"; // Use current directory instead of /tmp
                    int fd_tmp=mkstemp(tmp);
                    lock_fd(in,F_RDLCK);
                    lock_fd(fd_tmp,F_WRLCK);

                    char line2[BUF_SIZE];
                    size_t len2=0; ssize_t r2;
                    int found=0;
                    while ((r2=read(in,line2+len2,1))==1) {
                        len2++;
                        if (line2[len2-1]=='\n' || len2==BUF_SIZE-1) {
                            line2[len2]='\0';
                            if (len2>=strlen(cid)+1 && !memcmp(line2,cid,strlen(cid)) && line2[strlen(cid)]==':') {
                                found=1;
                                // parse existing list
                                char *rest = line2 + strlen(cid) + 1;
                                int dup=0;
                                char copy[BUF_SIZE]; strcpy(copy,rest);
                                for (char *p=strtok(copy,"," ); p; p=strtok(NULL,"," )) {
                                    if (!strcmp(p,id)) { dup=1; break; }
                                }
                                if (dup) {
                                    write(fd_tmp,line2,len2);
                                    close(in);
                                    close(fd_tmp);
                                    rename(tmp,ENR_FILE);
                                    send_str(cfd,"Already enrolled.\n");
                                    continue;
                                }
                                else {
                                    write(fd_tmp,cid,strlen(cid));
                                    write(fd_tmp,":",1);
                                    write(fd_tmp,rest,strlen(rest));
                                    write(fd_tmp,",",1);
                                    write(fd_tmp,id,strlen(id));
                                    write(fd_tmp,"\n",1);
                                }
                            } else {
                                write(fd_tmp,line2,len2);
                            }
                            len2=0;
                        }
                    }
                    if (!found) {
                        write(fd_tmp,cid,strlen(cid));
                        write(fd_tmp,":",1);
                        write(fd_tmp,id,strlen(id));
                        write(fd_tmp,"\n",1);
                    }
                    close(in);
                    close(fd_tmp);
                    rename(tmp,ENR_FILE);
                    send_str(cfd,"Enrolled.\n");
                    // After successfully enrolling:
                    send_str(cfd, "Enrolled in course ");
                    send_str(cfd, cid);
                    send_str(cfd, ".\n");
                }
                else if (buf[0]=='2') {
                    send_str(cfd,"Enter courseID to unenroll: ");
                    read_line(cfd,buf,sizeof(buf)); trim(buf);
                    char *cid = buf;
                    int in = open(ENR_FILE,O_RDONLY);
                    char tmp[]="./tmp_enrXXXXXX"; // Use current directory instead of /tmp
                    int fd_tmp=mkstemp(tmp);
                    lock_fd(in,F_RDLCK);
                    lock_fd(fd_tmp,F_WRLCK);

                    char line2[BUF_SIZE];
                    size_t len2=0; ssize_t r2;
                    while ((r2=read(in,line2+len2,1))==1) {
                        len2++;
                        if (line2[len2-1]=='\n' || len2==BUF_SIZE-1) {
                            line2[len2]='\0';
                            if (len2>=strlen(cid)+1 && !memcmp(line2,cid,strlen(cid)) && line2[strlen(cid)]==':') {
                                char *rest = line2 + strlen(cid) + 1;
                                char kept[BUF_SIZE] = "";
                                int first=1;
                                char copy[BUF_SIZE]; strcpy(copy,rest);
                                for (char *p=strtok(copy,"," ); p; p=strtok(NULL,"," )) {
                                    if (strcmp(p,id)) {
                                        if (!first) strcat(kept,",");
                                        strcat(kept,p);
                                        first=0;
                                    }
                                }
                                if (kept[0]) {
                                    write(fd_tmp,cid,strlen(cid));
                                    write(fd_tmp,":",1);
                                    write(fd_tmp,kept,strlen(kept));
                                    write(fd_tmp,"\n",1);
                                }
                            } else {
                                write(fd_tmp,line2,len2);
                            }
                            len2=0;
                        }
                    }
                    close(in);
                    close(fd_tmp);
                    rename(tmp,ENR_FILE);
                    send_str(cfd,"Unenrolled.\n");
                }
                else if (buf[0]=='3') {
                    send_str(cfd,"Your courses:\n");
                    int ef = open(ENR_FILE,O_RDONLY);
                    if (ef < 0) {
                        send_str(cfd,"No courses found.\n");
                        continue;
                    }
                    
                    lock_fd(ef,F_RDLCK);
                    char buf2[BUF_SIZE];
                    size_t len2=0; ssize_t r2;
                    int found_any = 0;
                    
                    while ((r2 = read(ef, buf2+len2,1))==1) {
                        len2++;
                        if (buf2[len2-1]=='\n' || len2==BUF_SIZE-1) {
                            buf2[len2]='\0';
                            
                            // Get the course ID
                            char *colon = strchr(buf2, ':');
                            if (colon) {
                                char cid[BUF_SIZE] = {0};
                                strncpy(cid, buf2, colon - buf2);
                                cid[colon - buf2] = '\0';
                                
                                // Check if student ID is in the enrollment list
                                char *student_list = colon + 1;
                                char list_copy[BUF_SIZE];
                                strcpy(list_copy, student_list);
                                trim(list_copy); // Add this to remove newlines and spaces
                                
                                // Debug output
                                char debug_msg[BUF_SIZE];
                                snprintf(debug_msg, sizeof(debug_msg), "Checking course %s with students: %s\n", cid, list_copy);
                                
                                // Parse list and check each student
                                int found_in_this_course = 0;
                                for (char *p = strtok(list_copy, ","); p; p = strtok(NULL, ",")) {
                                    trim(p); // Trim each ID to remove any whitespace
                                    
                                    // Debug: check what we're comparing
                                    snprintf(debug_msg, sizeof(debug_msg), "Comparing '%s' with '%s'\n", p, id);
                                    
                                    if (strcmp(p, id) == 0) {
                                        found_in_this_course = 1;
                                        found_any = 1;
                                        
                                        // Look up course name in courses.txt
                                        FILE *cf = fopen(CRS_FILE, "r");
                                        char course_info[BUF_SIZE] = "";
                                        char cline[BUF_SIZE];
                                        
                                        while (cf && fgets(cline, sizeof(cline), cf)) {
                                            trim(cline);
                                            if (strncmp(cline, cid, strlen(cid)) == 0 && cline[strlen(cid)] == ':') {
                                                char *fields[4], *c = cline;
                                                int i = 0;
                                                while (i < 4 && c) {
                                                    fields[i++] = c;
                                                    c = strchr(c, ':');
                                                    if (c) *c++ = '\0';
                                                }
                                                
                                                if (i >= 2) {
                                                    snprintf(course_info, sizeof(course_info), 
                                                             "Course ID: %s, Name: %s\n", fields[0], fields[1]);
                                                }
                                                break;
                                            }
                                        }
                                        if (cf) fclose(cf);
                                        
                                        if (course_info[0]) {
                                            send_str(cfd, course_info);
                                        } else {
                                            send_str(cfd, "Course ID: ");
                                            send_str(cfd, cid);
                                            send_str(cfd, "\n");
                                        }
                                        break;
                                    }
                                }
                            }
                            len2=0;
                        }
                    }
                    
                    if (!found_any) {
                        send_str(cfd, "You are not enrolled in any courses.\n");
                    }
                    
                    close(ef);
                }
                else if (buf[0]=='4') {
                    send_str(cfd,"Enter new password: ");
                    read_line(cfd,buf,sizeof(buf)); trim(buf);
                    // read existing name,status
                    FILE *sf = fopen(STUD_FILE,"r");
                    char tmpb[BUF_SIZE], nm[BUF_SIZE]={0}, st[BUF_SIZE]="active";
                    while (fgets(tmpb,sizeof(tmpb),sf)) {
                        trim(tmpb);
                        if (!strncmp(tmpb,id,strlen(id)) && tmpb[strlen(id)]==':') {
                            char *fld[4], *p2=tmpb; int j=0;
                            while (j<4 && p2) { fld[j++]=p2; p2=strchr(p2,':'); if (p2)*p2++ = '\0'; }
                            strcpy(nm,fld[1]);
                            strcpy(st,fld[3]);
                            break;
                        }
                    }
                    fclose(sf);
                    char rec[BUF_SIZE];
                    snprintf(rec,sizeof(rec),"%s:%s:%s:%s",id,nm,buf,st);
                    rewrite_single_line_sys(STUD_FILE,id,rec,cfd);
                    send_str(cfd,"Password changed.\n");
                }
                else send_str(cfd,"Invalid\n");
            }
        }
    }
    close(cfd);
    exit(0);
}

int main(){
    mkdir("data",0755);
    open(STUD_FILE, O_CREAT,0644);
    open(FAC_FILE,  O_CREAT,0644);
    open(CRS_FILE,  O_CREAT,0644);
    open(ENR_FILE,  O_CREAT,0644);

    int sfd = socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in sa = {
        .sin_family    = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port      = htons(PORT)
    };
    bind(sfd,(struct sockaddr*)&sa,sizeof(sa));
    listen(sfd,BACKLOG);

    while (1) {
        int cfd = accept(sfd,NULL,NULL);
        if (cfd<0) continue;
        if (fork()==0) {
            close(sfd);
            handle_client(cfd);
        }
        close(cfd);
        waitpid(-1,NULL,WNOHANG);
    }
    return 0;
}


/*
make
gcc -o server server.c
 ./server
make clean-> rm -f server
telnet localhost 9000 : to run client
 admin name: admin
 admin pwd: admin123

 install tellnet and then run
*/