#include <iostream>
#include <fstream>
#include "connection.hpp"

using namespace timax;
void connection::read_head(int offset)
{
	auto self = this->shared_from_this();
	socket_.async_read_some(boost::asio::buffer(read_buf_ + offset, 4096 - offset), [this, self, offset]
		(const boost::system::error_code& ec, std::size_t bytes_transferred)
	{
		size_t statas_code = 200;
		if (ec != 0)
		{
			if (ec == boost::asio::error::eof)
			{
				//std::cout << "client socket shutdown" << std::endl;
			}

			//std::cout <<ec.value()<<" reason: "<< ec.message() << std::endl;
			close();
			return;
		}

		if (offset + bytes_transferred > 4096)
		{
			statas_code = 413;
			std::cout << "parse error" << std::endl;
			//response(statas_code, true, self, request);
			return;
		}

		//request_t request;
		//int r = request.parse(read_buf_, offset+bytes_transferred, offset);
		//if (r == -2)
		//{
		//	std::cout << "incomplete \n";
		//	read_head(offset + bytes_transferred);
		//	return;
		//}

		bool need_close = true;// need_close_conneciton(request);

		//if (r < 0)
		//{
		//	statas_code = 400;
		//	std::cout << "parse error" << std::endl;
		//	response(statas_code, need_close, self, request);
		//	return;
		//}

		size_t body_len = 0;//request.body_length();
		if (body_len == 0)
		{
			boost::asio::async_write(socket_, boost::asio::buffer(g_str), [this, self](const boost::system::error_code& ec, std::size_t bytes_transferred) 
			{
				if (ec)
					std::cout << ec.message() << std::endl;

				close();
			});
			//response(statas_code, need_close, self, request);
			//if (request.has_keepalive_attr())
			//{
			//	response(statas_code, need_close, self, request);
			//}
			//else
			//{
			//	if (need_close)
			//	{
			//		close();
			//	}
			//	else
			//	{
			//		read_head();
			//	}
			//}
		}
		else
		{
			//std::cout << body_len << std::endl;
			//if (body_len + bytes_transferred>8192)
			//{
			//	statas_code = 413;
			//	response(statas_code, need_close, self, request);
			//	return;
			//}
			//read_body(self, need_close, std::move(request), body_len);
		}
	});
}

void connection::read_body(const std::shared_ptr<connection>& self, bool need_close, request_t request, size_t body_len)
{
	//read http body
	boost::asio::async_read(socket_, boost::asio::buffer(read_buf_), boost::asio::transfer_exactly(body_len),
		[this, self, need_close, req = std::move(request)]
	(const boost::system::error_code& ec, std::size_t bytes_transferred) mutable
	{
		if (ec)//on error
		{
			close();
			return;
		}

		int statas_code = 200;

		try
		{
			response(statas_code, need_close, self, req);
		}
		catch (const std::exception& ex)
		{
			std::cout << ex.what() << std::endl;
			statas_code = 400;
			response(statas_code, need_close, self, req);
		}
		catch (...)
		{
			statas_code = 400;
			response(statas_code, need_close, self, req);
		}
	});
}

void connection::response(size_t status_code, bool need_close, const std::shared_ptr<connection>& self, request_t& request)
{
	auto response = std::make_shared<response_t>(this, request.minor_version());
	response->set_status(status_code);
	//response->add_body("hello world");
	//callback to user
	//bool success = server_->process_route(&request, response.get());
	//if (!success)
	//{
	//	success = response_file(request, response.get(), need_close);
	//}

	//if (!success)
	//{
	//	std::cout<<"<Server> Router_error: %s - Responding with 404.\n";
	//	response->set_status(404, true);
	//	need_close = true;
	//}

	response->send_response(need_close);
}

bool connection::response_file(request_t& req, response_t* res, bool need_close)
{
	if (server_->static_dir().empty())
	{
		std::cout << "<Server> Response_file_error: %s - Responding with 404.\n" << std::endl;
		return false;
	}

	std::string path = server_->static_dir() + req.url().to_string();
	std::fstream in(path, std::ios::binary | std::ios::in);
	if (!in)
	{
		return false;
	}

	in.seekg(0, std::ios::end);
	size_t resource_size_bytes =static_cast<std::size_t>(in.tellg());
	if (resource_size_bytes > 1000*1000*2000) //too long
		return false;

	std::string header =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: " + http::content_type(path) + "\r\n"
		"Content-Length: " + boost::lexical_cast<std::string>(resource_size_bytes) + "\r\n";

	if (!need_close)
	{
		header += "Connection: Keep-Alive\r\n";
	}
	header += "\r\n";

	in.seekg(0, std::ios::beg);

	resource_buffer_.reset(new char[resource_size_bytes]);

	in.read(resource_buffer_.get(), resource_size_bytes);
	res->add_resource(resource_buffer_.get(), resource_size_bytes);

	return true;
}

void connection::write(const std::shared_ptr<response_t>& response, const std::vector<boost::asio::const_buffer>& buffers, bool need_close)
{
	auto self = this->shared_from_this();
	boost::asio::async_write(socket_, buffers,
		[this, self, response, need_close](const boost::system::error_code& ec, std::size_t bytes_transferred)
	{
		if (ec != 0)
		{
			std::cout << ec.message() << std::endl;
		}

		boost::system::error_code ignored_ec;
		socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);
		std::cout << "need close: " << need_close << std::endl;
		if (need_close)
			close();
		else
			read_head();
	});
}