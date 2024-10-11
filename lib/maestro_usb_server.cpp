#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include <boost/spirit/include/qi.hpp>
#include <libusb-1.0/libusb.h>
#include "protocol.h"

using boost::asio::ip::udp;

using namespace std;
using namespace boost;

namespace qi = boost::spirit::qi;

class PololuMaestro {
public:
  PololuMaestro() {
    const unsigned short vendorId = 0x1ffb;
    unsigned short productIDArray[] = {0x0089, 0x008a, 0x008b, 0x008c};
    libusb_init(&ctx);
    int count = libusb_get_device_list(ctx, &device_list);

    for(int i=0;i<count;i++) {
      device = device_list[i]; {
        cout << "Found device\n";
        break;
      }
    }

    libusb_free_device_list(device_list, 0);
    //libusb_exit(ctx);
  }

  struct __attribute__((__packed__)) servo {
    int16_t position;
    int16_t target;
    uint16_t speed;
    uint8_t acceleration;
  };

  struct maestro_variables {
    // The number of values on the data stack (0-32).  A value of 0 means the stack is empty.
    uint8_t stackPointer;

    /// The number of return locations on the call stack (0-10).  A value of 0 means the stack is empty.
    uint8_t callStackPointer;

    /// The error register.  Each bit stands for a different error (see uscError).
    /// If the bit is one, then it means that error occurred some time since the last
    /// GET_ERRORS serial command or CLEAR_ERRORS USB command.
    uint16_t errors;

    /// The address (in bytes) of the next bytecode instruction that will be executed.
    uint16_t programCounter;

    /// <remarks>This is public to avoid mono warning CS0169.</remarks>
    int16_t buffer[3];

    /// The data stack used by the script.  The values in locations 0 through stackPointer-1
    /// are on the stack.
    int16_t stack[32];

    /// The call stack used by the script.  The addresses in locations 0 through
    /// callStackPointer-1 are on the call stack.  The next return will make the
    /// program counter go to callStack[callStackPointer-1].
    uint16_t callStack[10];

    /// 0 = script is running.
    /// 1 = script is done.
    /// 2 = script will be done as soon as it executes one more instruction
    ///     (used to implement step-through debugging features)
    uint8_t scriptDone;

    /// <remarks>This is public to avoid mono warning CS0169.</remarks>
    uint8_t buffer2;

    servo servos[6];
  };

  bool deviceMatchesVendorProduct(libusb_device *device, unsigned short idVendor, unsigned short idProduct) {
    libusb_device_descriptor desc;
    libusb_get_device_descriptor(device, &desc);
    return idVendor == desc.idVendor && idProduct == desc.idProduct;
  }

  uint16_t getPosition(unsigned int channel) {
    maestro_variables vars;
    libusb_device_handle *device_handle;

    libusb_open(device, &device_handle);
    libusb_control_transfer(device_handle, 0xC0, REQUEST_GET_VARIABLES, 0, 0, (unsigned char*)&vars, (ushort)sizeof(uscVariables), (ushort)5000);
    libusb_close(device_handle);

    return vars.servos[channel].position;
  }

  void setTarget(uint8_t channel, uint16_t target) {
    libusb_device_handle *device_handle;

    libusb_open(device, &device_handle);
    libusb_control_transfer(device_handle, 0x40, REQUEST_SET_TARGET, target, channel, 0, 0, (ushort)5000);
    libusb_close(device_handle);
  }

  void setSpeed(uint8_t channel, uint16_t speed) {
    libusb_device_handle *device_handle;

    libusb_open(device, &device_handle);
    libusb_control_transfer(device_handle, 0x40, REQUEST_SET_SERVO_VARIABLE, speed, channel, 0, 0, (ushort)5000);
    libusb_close(device_handle);
  }

  void setAcceleration(uint8_t channel, uint16_t accel) {
    libusb_device_handle *device_handle;

    libusb_open(device, &device_handle);
    libusb_control_transfer(device_handle, 0x40, REQUEST_SET_SERVO_VARIABLE, accel, channel | 0x80, 0, 0, (ushort)5000);
    libusb_close(device_handle);
  }

private:
  uint8_t low(uint16_t n) {
    return n & 0x7F;
  }

  uint8_t high(uint16_t n) {
    return (n >> 7) & 0x7F;
  }

  libusb_context *ctx;
  libusb_device *device;
  libusb_device **device_list;
};

class Servo {
public:
    Servo(PololuMaestro *_driver, uint8_t _channel) {
        driver = _driver;
        channel = _channel;
    }

    void setAngle(uint16_t angle) {
      float ratio = (angle - (float)min_angle) / ((float)max_angle - (float)min_angle);

      float pos = (float)min_ms + (ratio * ((float)max_ms - (float)min_ms));

      driver->setTarget(channel, (uint16_t)pos * 4);
    }

    void setSpeed(uint16_t speed) {
      driver->setSpeed(channel, speed);
    }

    void setAcceleration(uint16_t accel) {
      driver->setAcceleration(channel, accel);
    }

    float getPosition() {
      float ms = (float)(driver->getPosition(channel)) / 4.0;
      float pos = (ms - min_ms) / (max_ms - min_ms);

      return min_angle + (pos * (max_angle - min_angle));
    }

    float getMs() {
      return driver->getPosition(channel) / 4.0;
    }

    void inc(int16_t i) {
      setAngle(getPosition() + i);
    }

    void minAngle(uint16_t _min_angle) {
      min_angle = _min_angle;
    }

    void maxAngle(uint16_t _max_angle) {
      max_angle = _max_angle;
    }

    void minMs(uint16_t _min_ms) {
      min_ms = _min_ms;
    }

    void maxMs(uint16_t _max_ms) {
      max_ms = _max_ms;
    }

private:
    PololuMaestro *driver;
    uint8_t channel;
    uint16_t min_angle;
    uint16_t max_angle;
    uint16_t min_ms;
    uint16_t max_ms;
};

PololuMaestro driver;
Servo pan(&driver, 0);
Servo tilt(&driver, 1);

class UdpServer {
public:
  UdpServer(boost::asio::io_service& io_service) : socket_(io_service, udp::endpoint(udp::v4(), 31337)) {
    start_receive();
  }

private:
  void start_receive() {
    socket_.async_receive_from(
      boost::asio::buffer(recv_buffer_), remote_endpoint_,
      boost::bind(&UdpServer::handle_receive, this,
        boost::asio::placeholders::error,
        boost::asio::placeholders::bytes_transferred));
  }

  void handle_receive(const boost::system::error_code& error, std::size_t) {
    if (!error || error == boost::asio::error::message_size) {
      try {
        int16_t x;
        int16_t y;

        std::string cmd(recv_buffer_.begin(), recv_buffer_.end());

        std::string::iterator begin = cmd.begin();
        std::string::iterator end = cmd.end();

        using qi::int_;
        using qi::ascii::space;

        qi::phrase_parse(begin, end, int_ >> int_, space, x, y);
        //printf("%i %i\n", x, y);

        pan.inc(x/10);
        tilt.inc(y/10);
      } catch (boost::system::system_error& e) {
        cout << "doh: " << e.what() << endl;
      }
    }

    start_receive();
  }

  void handle_send(boost::shared_ptr<std::string>, const boost::system::error_code&, std::size_t) {
  }

  udp::socket socket_;
  udp::endpoint remote_endpoint_;
  boost::array<char, 100> recv_buffer_;
};



int main(int argc, char* argv[]) {
  try {
    cout << "Starting ...\n";

    pan.minAngle(448);
    pan.maxAngle(2496);
    pan.minMs(800);
    pan.maxMs(2200);
    pan.setSpeed(1000);
    pan.setAcceleration(8);

    tilt.minAngle(400);
    tilt.maxAngle(2000);
    tilt.minMs(800);
    tilt.maxMs(2200);
    tilt.setSpeed(1000);
    tilt.setAcceleration(2);

    cout << "Listening ...\n";

    boost::asio::io_service io_service;
    UdpServer server(io_service);
    io_service.run();
  } catch(boost::system::system_error& e) {
    cout << "Error: " << e.what() << endl;
    return 1;
  }
}