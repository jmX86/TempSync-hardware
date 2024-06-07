from socket import socket, AF_INET, SOCK_STREAM


def returnDataForSend(useIPforBroker: bool, brokerAddress: str, brokerPort: int, useCredentials: bool, username="", password=""):
    listOfBytes = list()

    listOfBytes.append(254)

    if len(brokerAddress) > 63:
        raise ValueError("Address too long")
    if len(username) > 31:
        raise ValueError("Username too long")
    if len(password) > 31:
        raise ValueError("Password too long")

    if useIPforBroker:
        listOfBytes.append(0)

        print(brokerAddress)

        brokerAddress = brokerAddress.split('.')
        for i in brokerAddress:
            octet = int(i)
            if octet < 0 or octet > 255:
                raise ValueError("Invalid IP address")
            listOfBytes.append(octet)

        for i in range(64):
            listOfBytes.append(ord('\0'))
    else:
        listOfBytes.append(1)

        for i in brokerAddress:
            character = ord(i)
            if character < 0 or character > 255:
                raise ValueError("Invalid domain name")
            listOfBytes.append(ord(i))

        for i in range(64 - len(brokerAddress)):
            listOfBytes.append(ord('\0'))

    brokerPort = brokerPort.to_bytes(2, byteorder='big')
    for p in brokerPort:
        listOfBytes.append(int(p))

    if useCredentials:
        listOfBytes.append(1)
        for i in username:
            character = ord(i)
            if character < 0 or character > 255:
                raise ValueError("Invalid username")
            listOfBytes.append(ord(i))

        for i in range(32 - len(username)):
            listOfBytes.append(ord('\0'))

        for i in password:
            character = ord(i)
            if character < 0 or character > 255:
                raise ValueError("Invalid password")
            listOfBytes.append(ord(i))

        for i in range(32 - len(password)):
            listOfBytes.append(ord('\0'))
    else:
        listOfBytes.append(0)
        for i in range(64):
            listOfBytes.append(ord('\0'))

    print("Sending:", listOfBytes)
    return bytearray(listOfBytes)


def send_data(data):
    s = socket(AF_INET, SOCK_STREAM)
    s.connect(('192.168.8.10', 35252))
    s.send(data)
    s.close()


if __name__ == '__main__':
    useIP = input("Do you want to use IP for broker[Yes/No]: ")
    if useIP == "Yes":
        useIP = True
    elif useIP == "No":
        useIP = False
    else:
        raise ValueError

    brokerAddress = input("Broker address: ")
    brokerPort = int(input("Broker port: "))

    useCred = input("Does the broker require authentication[Yes/No]: ")
    if useCred == "Yes":
        useCred = True
        username = input("Username: ")
        password = input("Password: ")

        send_data(returnDataForSend(useIP, brokerAddress, brokerPort, useCred, username, password))
    elif useCred == "No":
        useCred = False

        send_data(returnDataForSend(useIP, brokerAddress, brokerPort, useCred))
    else:
        raise ValueError

    print("Done.")
