#ifndef REQUESTDATA
#define REQUESTDATA

#include <string>
#include <unordered_map>

constexpr int STATE_PARSE_URI = 1;
constexpr int STATE_PARSE_HEADERS = 2;
constexpr int STATE_RECV_BODY = 3;
constexpr int STATE_ANALYSIS = 4;
constexpr int STATE_FINISH = 5;

constexpr int MAX_BUFF = 4096;

// 有请求出现但是读不到数据,可能是Request Aborted,
// 或者来自网络的数据没有达到等原因,
// 对这样的请求尝试超过一定的次数就抛弃
constexpr int AGAIN_MAX_TIMES = 200;

constexpr int PARSE_URI_AGAIN = -1;
constexpr int PARSE_URI_ERROR = -2;
constexpr int PARSE_URI_SUCCESS = 0;

constexpr int PARSE_HEADER_AGAIN = -1;
constexpr int PARSE_HEADER_ERROR = -2;
constexpr int PARSE_HEADER_SUCCESS = 0;

constexpr int ANALYSIS_ERROR = -1;
constexpr int ANALYSIS_SUCCESS = 0;

constexpr int METHOD_POST = 1;
constexpr int METHOD_GET = 2;
constexpr int HTTP_10 = 1;
constexpr int HTTP_11 = 2;

constexpr int EPOLL_WAIT_TIME = 500;

class MimeType
{
private:
	static pthread_mutex_t lock;
	static std::unordered_map<std::string, std::string> mime;
	MimeType();
	MimeType(const MimeType& num);

public:
	static std::string getMime(const std::string& suffix);
};

enum HeadersState
{
	h_start = 0,
	h_key,
	h_colon,
	h_spaces_after_colon,
	h_value,
	h_CR,
	h_LF,
	h_end_CR,
	h_end_LF
};

struct mytimer;
struct requestData;

struct requestData
{
private:
	int againTimes;
	std::string path;
	int fd;
	int epollfd;
	// content部分用完就清
	std::string content;
	int method;
	int HTTPversion;
	std::string file_name;
	int now_read_pos;
	int state;
	int h_state;
	bool isfinish;
	bool keep_alive;
	std::unordered_map<std::string, std::string> headers;
	mytimer* timer;

private:
	int parse_URI();
	int parse_Headers();
	int analysisRequest();

public:
	requestData();
	requestData(int _epollfd, int _fd, std::string _path);
	~requestData();
	void addTimer(mytimer* mtimer);
	void reset();
	void seperateTimer();
	int getFd();
	void setFd(int _fd);
	void handleRequest();
	void handleError(int fd, int err_num, std::string shrot_msg);
};

struct mytimer
{
	bool deleted;
	size_t expired_time;
	requestData* request_data;
	mytimer(requestData* request_data, int timeout);
	~mytimer();
	void update(int timeout);
	bool isvalid();
	void clearReq();
	void setDeleted();
	bool isDeleted() const;
	size_t getExpTime() const;
};

struct timerCmp
{
	bool operator()(const mytimer* timer1, const mytimer* timer2) const;
};

#endif