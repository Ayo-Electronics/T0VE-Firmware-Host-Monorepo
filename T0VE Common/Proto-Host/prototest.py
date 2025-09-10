from proto_messages import App_Messages


def main():
    # Create a Command message
    msg = App_Messages.Command(command_id=42, command_data="Hello device!")

    # Print the object
    print("Original object:", msg)

    # Serialize to protobuf wire format
    encoded = msg.SerializeToString()
    print("Serialized bytes:", encoded)

    # Deserialize back into a new object
    decoded = App_Messages.Command().parse(encoded)
    print("Decoded object:", decoded)

    # Verify the round-trip preserved values
    assert decoded.command_id == msg.command_id
    assert decoded.command_data == msg.command_data
    print("✅ Round-trip encode/decode successful!")


if __name__ == "__main__":
    main()
