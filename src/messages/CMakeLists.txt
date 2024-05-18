find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})
message(STATUS "Protobuf compiler path: ${PROTOBUF_PROTOC_EXECUTABLE}")
# Find all .proto files in the current directory
file(GLOB PROTO_FILES "*.proto")

# Generate C++ source files from .proto files
PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

add_library(messages SHARED ${PROTO_SRCS})
