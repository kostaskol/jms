#include <parser.h>
#include <fcntl.h>
#include <iostream>
#include <pipemanager.h>
#include <message.h>
#include <cstring>
#include <constants.h>
#include "console.h"
#include "helpfunc.h"

using namespace std;

#define WRITE 0
#define READ 1

console::console(cmd_arguments &args) : _op(args.get_op()), _r_pipe(args.get_r_pipe()), _w_pipe(args.get_w_pipe()) {
        // pipe[0] -> writer
        pipes.push(pipe_manager(_w_pipe, O_WRONLY));

        // pipe[1] -> reader
        pipes.push(pipe_manager(_r_pipe, O_RDONLY));
}

console::~console() {}

void console::start() {
    parser p(_op);
    cout << "Waiting on initial handshake" << endl;

    if (pipes.at(WRITE).write_msg((char *)"ack?") < 0) {
        perror("Pipe write error");
        return;
    }

    my_string message;

    if (pipes.at(READ).read_msg(&message) < 0) {
        perror("Pipe read error");
        return;
    }

    if (!(message == "ack")) {
        cerr << "Not acknowledged" << endl;
        return;
    }

    cout << "Handshake exchanged" << endl;

    my_string line;

    str_vec com;
    bool end;
    do {
        if (!end) {
	    // void parser::next_command(my_vector *) reads a line from
	    // the operations file and turns it into a my_vector object
	    // Returns true if we have reached EOF
	    end = p.next_command(&com);
            if (end) {
                cout << "Operations file complete. Input your command below"
                     << endl;
            }
        } else {
            cout << "$ ";
            char *buf = new char[201];
            fgets(buf, 201, stdin);

            line = buf;
            delete[] buf;
            line.remove('\n');
            com = line.split(' ');
        }

	  // int hf::get_com(my_vector, bool) 
	  // figures out the required operation and returns
	  // an integer value (defined in shared/include/Constants.h)
          switch(hf::get_com(com, true)) {
            case C_SUB:
              cout << "Submitting " << endl;
              _submit(com);
                break;
            case C_STAT:
              cout << "Status" << endl;
              try {
                _status(com);
                break;
              } catch(exception &e) {
                cout << "Malformed command" << endl;
                continue;
              }
            case C_STAT_ALL: {
                cout << "Status-all" << endl;
                _status_all(com);
                break;
            }
            case C_SHOW_ACT:
                cout << "Show active" << endl;
                _show_active(com);
                break;
            case C_SHOW_POOL:
                cout << "Show pool" << endl;
                _show_pool(com);
                break;
            case C_SHOW_FIN:
                cout << "Show finished" << endl;
                _show_finished(com);
                break;
            case C_SUSP: {
                cout << "Suspend" << endl;
                _suspend(com);
                break;
            }
            case C_RES: {
                cout << "Resume" << endl;
                _resume(com);
                break;
            }
            case C_SD:
                cout << "Shutdown" << endl;
                _sd(com);
                break;
            default:
                cout << "Console: Unknown command: " << com.at(0) << endl;
          }
    } while (true);

}

void console::_submit(str_vec job) {
    // You can find an explanation about the 
    // communication steps required in README.txt:4.2
    msg::message enc(job, PROT_REQ);

    enc.encode();

    if (pipes.at(0).write_msg(enc) < 0) {
        perror("Write");
        return;
    }

    my_string resp_str;
    if (pipes.at(1).read_msg(&resp_str) < 0) {
        perror("Read");
        return;
    }

    msg::message resp_msg(resp_str, PROT_RESP);

    resp_msg.decode();

    str_vec resp_vec = resp_msg.get_list();

    if (resp_vec.size() == 0) {
      cout << "Bad size" << endl;
        return;
    }

    if (resp_vec.at(0)[0] != enc.get_message()[0]) {
        cout << "Unexpected response";
        cout << "(Found " << resp_vec.at(0)[0] << " expecting "
             << enc.get_message()[0] << ")" << endl;
        return;
    }

    for (size_t i = 1; i < job.size(); i++) {
      cout << job.at(i) << " ";
    }

    cout << endl;
    cout << "Job PID: " << resp_vec.at(2) << ", Job's ID: " << resp_vec.at(1)
         << endl;

}

void console::_status(str_vec jid) {
    msg::message req_msg(jid, PROT_REQ);
    req_msg.encode();

    if (pipes.at(WRITE).write_msg(req_msg) < 0) {
        perror("Console: Write");
        exit(-1);
    }

    my_string resp;
    int bytes;
    bytes = pipes.at(READ).read_msg(&resp);
    if (bytes < 0) {
        perror("Console: Read");
        exit(-1);
    }

    if (bytes == 0) {
        cout << "Console: Reached EOF" << endl;
        return;
    }

    msg::message resp_msg(resp, PROT_RESP);
    resp_msg.decode();

    str_vec resp_vec = resp_msg.get_list();


    if (resp_vec.at(1) == "3") {
        cout << "Invalid job id: " << jid.at(1) << endl;
        return;
    }

    cout << "Job ID & status" << endl;
    cout << jid.at(1) << ". ";
    try {
        if (resp_vec.at(1) == "0") {
            cout << "Active (Running for " << resp_vec.at(2) << " seconds)" << endl;
        } else if (resp_vec.at(1) == "1") {
            cout << "Suspended" << endl;
        } else if (resp_vec.at(1) == "2") {
            cout << "Finished" << endl;
        } else {
            cout << "Unknown state: " << resp_vec.at(2) << endl;
        }
    } catch(exception &e) {
        cout << "Malformed response" << endl;
    }
}

void console::_status_all(str_vec time_lim) {
    msg::message req_msg(time_lim, PROT_REQ);
    req_msg.encode();

    pipes.at(WRITE).write_msg(req_msg);

    my_string resp;
    pipes.at(READ).read_msg(&resp);

    msg::message resp_msg(resp, PROT_RESP);

    resp_msg.decode();

    str_vec resp_vec = resp_msg.get_list();

    if (resp_vec.size() == 1) {
      cout << "No jobs have been submitted" << endl;
      return;
    }

    cout << "JobID & Job Status" << endl;
    for (size_t i = 1; i < resp_vec.size(); i++) {
      cout << i - 1 << ".   ";

      if (resp_vec.at(i)[0] == '0') {
        try {
          cout << "Active. Running for " << resp_vec.at(i).split('-').at(1);
        } catch (exception &e) {
          cout << "Active. Unknown running time" << endl;
        }
      } else if (resp_vec.at(i) == "1") {
        cout << "Suspended";
      } else if (resp_vec.at(i) == "2") {
        cout << "Finished";
      } else {
        cout << "Undefined status (" << resp_vec.at(i) << endl;
      }
      cout << endl;
    }
}

void console::_show_active(str_vec vec) {
    msg::message req_msg(vec, PROT_REQ);
    req_msg.encode();
    pipes.at(WRITE).write_msg(req_msg);

    my_string resp;
    pipes.at(READ).read_msg(&resp);
    msg::message resp_msg(resp, PROT_RESP);
    resp_msg.decode();

    str_vec resp_vec = resp_msg.get_list();
    if (resp_vec.size() == 2) {
        if (resp_vec.at(1) == "") {
            cout << "No active jobs" << endl;
            return;
        }
    }

    cout << "Active Jobs" << endl;
    for (size_t i = 1; i < resp_vec.size(); i++) {
        cout << i << ". ";
        cout << resp_vec.at(i) << endl;
    }
}

void console::_show_pool(str_vec pool) {
    msg::message req_msg(pool, PROT_REQ);
    req_msg.encode();

    pipes.at(WRITE).write_msg(req_msg);

    my_string resp;
    pipes.at(READ).read_msg(&resp);

    msg::message resp_msg(resp, PROT_RESP);
    resp_msg.decode();

    str_vec resp_vec = resp_msg.get_list();

    if (resp_vec.size() == 1) {
     cout << "There are no active pools" << endl;
     return;
    }

    cout << "Pool Pid & Number of jobs" << endl;
    for (size_t i = 1; i < resp_vec.size(); i++) {
      try {
        str_vec tmp_vec = resp_vec.at(i).split('-');
        tmp_vec.at(1);
        cout << i - 1 << ". ";
        cout << tmp_vec.at(0) << "\t" << tmp_vec.at(1) << endl;
      } catch (exception &e) {
        cout << "There are no active pools" << endl;
        break;
      }
    }
}

void console::_show_finished(str_vec fin) {
  msg::message req_msg(fin, PROT_REQ);
  req_msg.encode();

  pipes.at(WRITE).write_msg(req_msg);

  my_string resp;
  pipes.at(READ).read_msg(&resp);

  msg::message resp_msg(resp, PROT_RESP);
  resp_msg.decode();

  str_vec resp_vec = resp_msg.get_list();
  cout << "Finished Jobs: " << endl;

  for (size_t i = 1; i < resp_vec.size(); i++) {
    cout << i - 1 << ". ";
    cout << resp_vec.at(i) << endl;
  }
}

void console::_suspend(str_vec susp) {
  cout << "Suspend" << endl;
  if (susp.size() != 2) {
    cout << "Malformed suspend command" << endl;
    return;
  }

  msg::message req_msg(susp, PROT_REQ);
  req_msg.encode();

  pipes.at(WRITE).write_msg(req_msg);

  my_string resp;
  pipes.at(READ).read_msg(&resp);

  msg::message resp_msg(resp, PROT_RESP);
  resp_msg.decode();

  if (resp_msg.get_list().size() == 0) {
    cout << "Suspend: Malformed command" << endl;
    return;
  }

  if (resp_msg.get_list().at(0) != "6") {
        cout << "Suspend: Unexpected response: " << resp << endl;
        return;
  }

  my_string code = resp_msg.get_list().at(1);
    if (code == "K") {
      cout << "Job ID " << susp.at(1) << " successfully suspended" << endl;
    } else if (code == "X") {
      cout << "Unknown error while suspending Job " << susp.at(1) << endl;
    } else if (code == "E") {
      cout << "Job " << susp.at(1) << " is already suspended" << endl;
    } else if (code == "I") {
      cout << "Invalid Job ID " << susp.at(1) << endl;
    } else if (code == "F") {
      cout << "Job " << susp.at(1) << " has already finished" << endl;
    }

}

void console::_resume(str_vec res) {
  cout << "Resume" << endl;
  if (res.size() != 2) {
    cout << "Malformed resume command" << endl;
    return;
  }

  msg::message req_msg(res, PROT_REQ);
  req_msg.encode();

  pipes.at(WRITE).write_msg(req_msg);

  my_string resp;
  pipes.at(READ).read_msg(&resp);

  msg::message resp_msg(resp, PROT_RESP);
  resp_msg.decode();

  if (resp_msg.get_list().size() == 0) {
    cout << "Resume: Malformed response" << endl;
    return;
  }

  if (resp_msg.get_list().at(0) != "7") {
        cout << "Resume: Unexpected response: " << resp << endl;
        return;
  }
  my_string code = resp_msg.get_list().at(1);
  if (code == "K") {
    cout << "Job ID " << res.at(1) << " successfully resumed" << endl;
  } else if (code == "X") {
    cout << "Unknown error while resuming job " << res.at(1) << endl;
  } else if (code == "S") {
    cout << "Job " << res.at(1) << " was not suspended" << endl;
  } else if (code == "I") {
    cout << "Invalid Job ID: " << res.at(1) << endl;
  }
}

void console::_sd(str_vec sd) {
    msg::message msg(sd, PROT_REQ);
    msg.encode();
    cout << "Sending shutdown message" << endl;
    if (pipes.at(WRITE).write_msg(msg) < 0) {
        perror("Console: Write");
        cout << "Warning: Exiting without having sent the" <<
        " shutdown signal." << endl;
        exit(-1);
    }
    my_string resp;
    int bytes = pipes.at(READ).read_msg(&resp);
    if (bytes < 0) {
        perror("Console: Read");
        exit(-1);
    }

    msg::message resp_msg(resp, PROT_RESP);

    resp_msg.decode();

    str_vec resp_vec = resp_msg.get_list();

    if (!(resp_vec.at(0) == "8")) {
        cout << "Console: Unexpected response to shutdown: " << resp_vec.at(0)
             << endl;
        exit(-1);
    }

    try {
      cout << "Coordinator exited after serving " << resp_vec.at(1) << " jobs, ";
      cout << resp_vec.at(2) << " of which were active" << endl;
    } catch (exception &e) {
      cout << "Unexpected response from coordinator" << endl;
    }

    cout << "Everything shutdown normally. Console exiting" << endl;
    exit(0);
}
