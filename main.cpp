#include <iostream>
#include <map>
#include <random>

#include <fcntl.h>
#include <unistd.h>
#include <x86intrin.h>

#include "marketdata_generated.h"

struct OrderBook {
	inline OrderBook(int levels_count) noexcept
	{
		std::mt19937 rand_engine(42);
		std::uniform_int_distribution<int> dist(2, 40000);
		while (bids.size() != levels_count) { bids[dist(rand_engine)] = dist(rand_engine); }
		while (asks.size() != levels_count) { asks[dist(rand_engine)] = dist(rand_engine); }
	}
	std::map<double, double> bids;
	std::map<double, double> asks;
};

std::ostream& operator<< (std::ostream& os, const OrderBook& ob) {
	os << "=========== ASKS ==========" << std::endl;
	for (const auto& [p, q] : ob.asks) { os << p << " : " << q << std::endl; }
	os << "=========== BIDS ==========" << std::endl;
	for (const auto& [p, q] : ob.bids) { os << p << " : " << q << std::endl; }
	os << "=========== END  ==========" << std::endl;
	return os;
}

inline auto map_to_entries_vector(const std::map<double, double>& mp,
		flatbuffers::FlatBufferBuilder& buf_builder) {

	using namespace shift::fbs;
	std::vector<flatbuffers::Offset<L3Entry>> entries_vector;
	for (const auto& [p, q] : mp) {
		auto entry_str = buf_builder.CreateString("Entry");
		L3EntryBuilder entry_builder(buf_builder);
		entry_builder.add_px(p);
		entry_builder.add_amt(q);
		entry_builder.add_id(entry_str);
		auto entry = entry_builder.Finish();
		entries_vector.push_back(entry);
	}
	return buf_builder.CreateVector(entries_vector);
}

inline uint64_t log_order_book(const OrderBook& ob) noexcept
{
	uint64_t start = ::clock();

	using namespace shift::fbs;
	flatbuffers::FlatBufferBuilder buf_builder(1024); // init size is optional

	auto asks_buf_vec = map_to_entries_vector(ob.asks, buf_builder);
	auto bids_buf_vec = map_to_entries_vector(ob.bids, buf_builder);

	L3SnapshotMarketDataBuilder    l_3_snapshot_market_data_builder(buf_builder);
	l_3_snapshot_market_data_builder.add_flags(0xff);
	l_3_snapshot_market_data_builder.add_askEntries(asks_buf_vec);
	l_3_snapshot_market_data_builder.add_bidEntries(bids_buf_vec);
	auto l3_snap = l_3_snapshot_market_data_builder.Finish();

	auto market_data_event_text = buf_builder.CreateString("GG");

	MarketDataEventBuilder         market_data_event_builder(buf_builder);
	market_data_event_builder.add_type(MdType::MdType_L3Snapshot);
	market_data_event_builder.add_text(market_data_event_text);
	market_data_event_builder.add_productId(42);
	market_data_event_builder.add_l3smd(l3_snap);
	auto market_data_event = market_data_event_builder.Finish();

	auto session = CreateMarketDataCaptureSession(buf_builder, 420, 1337);

	MarketDataCaptureBuilder       market_data_builder(buf_builder);
	market_data_builder.add_type(MdCapType::MdCapType_MD);
	market_data_builder.add_session(session);
	market_data_builder.add_mde(market_data_event);
	auto md = market_data_builder.Finish();
	buf_builder.Finish(md);
	uint64_t end = ::clock();

	uint8_t* buf  = buf_builder.GetBufferPointer();
	int      size = buf_builder.GetSize();
	int     fd  = ::open("market_data_enc_dec.bin", O_TRUNC | O_CREAT | O_WRONLY, 0644);
	ssize_t ret = ::write(fd, buf, size);
	::close(fd);
	return end - start;
}

inline void measure_av_encode_time(int levels_count) noexcept
{
	std::vector<uint64_t> times;
	for (int i = 0; i < 1000; ++i) {
		OrderBook ob(levels_count);
		times.push_back(log_order_book(ob));
	}
	double sum = 0;
	for (auto t : times) {
		sum += t;
	}
	std::cout << levels_count << ": av time: " << sum / times.size() << " us" << std::endl;
}

inline OrderBook decode_order_book(const void* bin, size_t size, uint64_t* time) noexcept
{
	using namespace shift::fbs;
	OrderBook res(0);

	uint64_t start = ::clock();
	flatbuffers::Verifier verifier((const unsigned char*)bin, size);
	bool v = VerifyMarketDataCaptureBuffer(verifier);
	if (!v) { std::cout << "verify err" << std::endl; exit(0);}
	auto market_data_capture = GetMarketDataCapture(bin);
	if (market_data_capture->type() == MdCapType::MdCapType_MD) {
		if (market_data_capture->mde()->type() == MdType::MdType_L3Snapshot) {
			auto asks = market_data_capture->mde()->l3smd()->askEntries();
			auto bids = market_data_capture->mde()->l3smd()->bidEntries();
			for (const auto a : *asks) { res.asks[a->px()] = a->amt(); }
			for (const auto b : *bids) { res.bids[b->px()] = b->amt(); }
		} else {
			std::cout << "err1" << std::endl; exit(0);
		}
	} else {
		std::cout << "err2" << std::endl; exit(0);
	}
	uint64_t end = ::clock();
	*time = end - start;
	return res;
}

inline OrderBook decode_one_file(const char* filename, uint64_t* time) noexcept
{
	OrderBook res(0);
	FILE *f = fopen(filename, "rb+");
	char buf[4 * 1024 * 1024];
	if (f) {
		fseek(f, 0L, SEEK_END);
		long filesize = ftell(f); // get file size
		fseek(f, 0L ,SEEK_SET); //go back to the beginning
		int ret = fread(buf, 1, filesize, f);
		fclose(f);

		res = decode_order_book(buf, filesize, time);
	} else {
		std::cout << "File open error" << std::endl;
	}
	return res;
}

inline void measure_av_decode_time(int levels_count) noexcept
{
	double sizes_sum = 0;
	std::vector<uint64_t> times;

	for (int i = 0; i < 1000; ++i) {
		OrderBook ob(levels_count);
		log_order_book(ob);
		uint64_t time;
		auto res =  decode_one_file("market_data_enc_dec.bin", &time);
		times.push_back(time);
		sizes_sum += res.asks.size();
		sizes_sum += res.bids.size();
	}

	double sum = 0;
	for (auto t : times) {
		sum += t;
	}
	std::cout << levels_count << ": av time: " << sum / times.size() << " us. Levels count in parsed: "
	          << sizes_sum / 2000. << std::endl;
}

int main()
{
	std::cout << "WithOUT verification" << std::endl;
	measure_av_decode_time(1);
	measure_av_decode_time(10);
	measure_av_decode_time(100);
	measure_av_decode_time(1000);
	measure_av_decode_time(10000);

	return 0;
}
