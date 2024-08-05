find_package(Protobuf REQUIRED)
find_package(absl CONFIG REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})
message(STATUS "Protobuf include path: ${Protobuf_INCLUDE_DIRS}")
message(STATUS "Protobuf compiler path: ${PROTOBUF_PROTOC_EXECUTABLE}")
# Find all .proto files in the current directory
file(GLOB PROTO_FILES "*.proto")

# Generate C++ source files from .proto files
PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

add_library(messages SHARED ${PROTO_SRCS})

target_include_directories(messages PUBLIC "${CMAKE_BINARY_DIR}/src/messages" /usr/include/google)

# link abseil.
target_link_libraries(messages PUBLIC
  ${Protobuf_LIBRARIES}
  absl::algorithm absl::base absl::debugging absl::flat_hash_map
  absl::flags absl::memory absl::meta absl::numeric absl::random_random absl::strings absl::synchronization absl::time absl::utility)
