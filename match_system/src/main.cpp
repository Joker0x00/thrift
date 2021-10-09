#include "match_server/Match.h"
#include "save_client/Save.h"
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/transport/TTransportUtils.h>
#include <thrift/transport/TSocket.h>
#include <thrift/concurrency/ThreadManager.h>
#include <thrift/concurrency/ThreadFactory.h>
#include <thrift/server/TThreadPoolServer.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/TToString.h>


#include <unistd.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <algorithm>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;

using namespace ::match_service;
using namespace ::save_service;
using namespace std;


struct Task
{
    User user;
    string type;
};

struct MessageQueue
{
    queue<Task> q;
    mutex m;
    condition_variable cv;
}message_queue;


class Pool
{
    public:
        void save_result(int a, int b)
        {
            printf("Match Result: %d %d\n", a, b);


            std::shared_ptr<TTransport> socket(new TSocket("123.57.47.211", 9090));
            std::shared_ptr<TTransport> transport(new TBufferedTransport(socket));
            std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
            SaveClient client(protocol);

            try {
                transport->open();

                int res = client.save_data("acs_1597", "341c3f0d", a, b);

                if (!res) puts("success");
                else puts("failed");

                transport->close();
            } catch (TException& tx) {
                cout << "ERROR: " << tx.what() << endl;
            }
        }
        bool check_user(uint32_t i, uint32_t j)
        {
            auto a = users[i], b = users[j];
            int dt = abs(a.score - b.score);
            int a_max_dif = wt[i] * 50 , b_max_dif = wt[j] * 50;
         //   cout << a_max_dif << " " << b_max_dif << endl;
            return dt <= a_max_dif && dt <= b_max_dif;
        }
        void match()
        {
            for(uint32_t i = 0 ; i < wt.size() ; i ++)
                wt[i] ++;
            cout << "+++++" << endl;
            while (users.size() > 1)
            {
                bool flag = true;
                for(uint32_t i = 0 ; i < users.size() ; i ++)
                {
                    for(uint32_t j = i + 1 ; j < users.size() ; j ++)
                    {
                        auto a = users[i], b = users[j];
                        if(check_user(i, j))
                        {
                            users.erase(users.begin() + j);
                            users.erase(users.begin() + i);
                            wt.erase(wt.begin() + j);
                            wt.erase(wt.begin() + i);
                            save_result(a.id, b.id);
                            flag = false;
                            break;
                        }
                    }
                        
                    if(!flag)break;

                }

                if(flag)break;
           }
        }

        void add(User user)
        {
            users.push_back(user);
            wt.push_back(0);
        }

        void remove(User user)
        {
            for (uint32_t i = 0; i < users.size(); i ++ )
                if (users[i].id == user.id)
                {
                    users.erase(users.begin() + i);
                    wt.erase(wt.begin() + i);
                    break;
                }
        }

    private:
        vector<User> users;
        vector<int> wt;

}pool;


class MatchHandler : virtual public MatchIf {
    public:
        MatchHandler() {
            // Your initialization goes here
        }

        int32_t add_user(const User& user, const std::string& info) {
            // Your implementation goes here
            printf("add_user\n");

            unique_lock<mutex> lck(message_queue.m);
            message_queue.q.push({user, "add"});
            message_queue.cv.notify_all();

            return 0;
        }

        int32_t remove_user(const User& user, const std::string& info) {
            // Your implementation goes here
            printf("remove_user\n");

            unique_lock<mutex> lck(message_queue.m);
            message_queue.q.push({user, "remove"});
            message_queue.cv.notify_all();

            return 0;
        }

};


void consume_task()
{
    while (true)
    {
        unique_lock<mutex> lck(message_queue.m);
        if (message_queue.q.empty())
        {
            //message_queue.cv.wait(lck);
            lck.unlock();
            pool.match();
            cout << "-----" << endl;
            sleep(1);
        }
        else
        {
            auto task = message_queue.q.front();
            message_queue.q.pop();
            lck.unlock();
            cout << "else" << endl;
            if (task.type == "add") pool.add(task.user);
            else if (task.type == "remove") pool.remove(task.user);

        }
    }
}
class MatchCloneFactory : virtual public MatchIfFactory {
    public:
        ~MatchCloneFactory() override = default;
        MatchIf* getHandler(const ::apache::thrift::TConnectionInfo& connInfo) override
        {
            std::shared_ptr<TSocket> sock = std::dynamic_pointer_cast<TSocket>(connInfo.transport);
            return new MatchHandler;
        }
        void releaseHandler( MatchIf* handler) override {
            delete handler;
        }
};


int main(int argc, char **argv) {


    TThreadedServer server(
            std::make_shared<MatchProcessorFactory>(std::make_shared<MatchCloneFactory>()),
            std::make_shared<TServerSocket>(9090), //port
            std::make_shared<TBufferedTransportFactory>(),
            std::make_shared<TBinaryProtocolFactory>());


    cout << "Start Match Server" << endl;

    thread matching_thread(consume_task);

    server.serve();
    return 0;
}

