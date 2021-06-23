#include <chrono>
#include <cstring>
#include <iostream>
#include <type_traits>
#include <vector>

//types to be used
typedef std::chrono::high_resolution_clock Clock;

//the type of transaction
enum class TransactionType {
	INVALID = -1,
	GENERATE = 0,
	TRANSFER = 1,
	RECEIPT = 2,
};

//the amount to transfer to a new account
constexpr int blankSize = 4 * sizeof(unsigned);
struct Blank {
	TransactionType type;
	unsigned char unused[blankSize];
};

struct Transfer {
	TransactionType type;
	unsigned senderAccount;
	unsigned receiverAccount;
	unsigned prevReceipt; //prove this sender received coins previously (block index)
	unsigned amount; //amount to be transferred
};

struct Receipt {
	TransactionType type;
	unsigned account; //account to receive
	unsigned prevReceipt; //prior balance (block index)
	unsigned prevTransfer; //receiving money (block index)
	unsigned balance; //new balance
};

union Transaction {
	TransactionType type; //union signal
	Blank blank;
	Transfer transfer;
	Receipt receipt;
};

//the building block of the chain
struct Block {
	unsigned index;
	unsigned prevHash;
	Clock::duration timestamp;
	Transaction transaction;
	unsigned nonce;
	unsigned threshold; //store my own hash threshold
};

//checks
static_assert(std::is_pod<Transfer>::value, "Transfer is not a POD");
static_assert(std::is_pod<Receipt>::value, "Receipt is not a POD");
static_assert(std::is_pod<Transaction>::value, "Transaction is not a POD");
static_assert(std::is_pod<Block>::value, "Block is not a POD");

//variables for the blockchain proper
std::vector<Block> blockVector;

//hash a byte array into an unsigned 32-bit integer
unsigned fnv_hash_1a_32(void *key, int len) {
	unsigned char *p = static_cast<unsigned char*>(key);
	unsigned h = 0x811c9dc5;
	for (int i = 0; i < len; i++) {
		h = ( h ^ p[i] ) * 0x01000193;
	}
	return h;
}

Transaction generateBlank(const char data[blankSize]) {
	Transaction transaction;
	transaction.type = TransactionType::INVALID;
	memcpy(&transaction.blank.unused, data, blankSize);
	return transaction;
}

Transaction generateTransfer(unsigned sender, unsigned receiver, unsigned amount) {
	if (sender == receiver || receiver == 0) {
		return { TransactionType::INVALID };
	}

	//info about the sender
	unsigned balance = 0;
	unsigned prevSenderReceipt = -1;

	//validate that this sender has money to send (sender = 0 is a special case)
	if (sender != 0) {
		for (auto iter = blockVector.rbegin(); iter != blockVector.rend(); iter++) {
			if (iter->transaction.type == TransactionType::RECEIPT && iter->transaction.receipt.account == sender) {
				balance = iter->transaction.receipt.balance;
				prevSenderReceipt = iter->index;
				break;
			}
		}
	}

	if (sender != 0 && balance < amount) {
		return Transaction { TransactionType::INVALID };
	}

	//return the valid transaction for hashing
	Transaction transaction;
	transaction.transfer = {
		sender == 0 ? TransactionType::GENERATE : TransactionType::TRANSFER,
		sender,
		receiver,
		prevSenderReceipt,
		amount
	};
	return transaction;
}

Transaction generateReceipt(Block transferBlock) {
	//accepts generate & transfers
	if (transferBlock.transaction.type != TransactionType::GENERATE && transferBlock.transaction.type != TransactionType::TRANSFER) {
		return { TransactionType:: INVALID };
	}

	//info about the receiver
	unsigned balance = 0;
	unsigned prevReceiverReceipt = -1;

	//find the receiver's previous balance
	for (auto iter = blockVector.rbegin(); iter != blockVector.rend(); iter++) {
		if (iter->transaction.type == TransactionType::RECEIPT && iter->transaction.receipt.account == transferBlock.transaction.transfer.receiverAccount) {
			balance = iter->transaction.receipt.balance;
			prevReceiverReceipt = iter->index;
			break;
		}
	}

	//return the valid transaction for hashing
	Transaction transaction;
	transaction.receipt = {
		TransactionType::RECEIPT,
		transferBlock.transaction.transfer.receiverAccount, //account ID
		prevReceiverReceipt, //prior balance stored here
		transferBlock.index, //receiving money from here
		balance + transferBlock.transaction.transfer.amount //new balance
	};
	return transaction;
}

Transaction generateReturn(Block transferBlock, Block receiptBlock) {
	//accepts generate & transfers
	if (transferBlock.transaction.type != TransactionType::GENERATE && transferBlock.transaction.type != TransactionType::TRANSFER) {
		return { TransactionType:: INVALID };
	}

	//accepts receipts
	if (receiptBlock.transaction.type != TransactionType::RECEIPT) {
		return { TransactionType::INVALID };
	}

	//make sure the return can go somewhere correct (GENERATE blocks don't have a correct return address)
	if (transferBlock.transaction.transfer.prevReceipt == -1) {
		return { TransactionType::INVALID };
	}

	//get the prior balance
	unsigned balance = -1;

	for (auto iter = blockVector.rbegin(); iter != blockVector.rend(); iter++) {
		if (iter->index == transferBlock.transaction.transfer.prevReceipt) {
			balance = iter->transaction.receipt.balance;
			break;
		}
	}

	//return the remaining balance to the sender's account
	Transaction transaction;
	transaction.receipt = {
		TransactionType::RECEIPT,
		transferBlock.transaction.transfer.senderAccount,
		transferBlock.transaction.transfer.senderAccount,
		receiptBlock.index,
		balance - transferBlock.transaction.transfer.amount,
	};
	return transaction;
}

Block generateBlock(Transaction transaction, unsigned prevHash) {
	static unsigned blockCounter = 0;
	Block block;
	block.index = blockCounter++;
	block.prevHash = prevHash;
	block.timestamp = Clock::now().time_since_epoch();
	block.transaction = transaction;
	return block;
}

unsigned hashBlock(Block& block, unsigned const threshold) {
	unsigned hash = -1;
	unsigned nonce = 0;
	block.threshold = threshold;
	while (hash > threshold) {
		block.nonce = nonce++;
		hash = fnv_hash_1a_32(&block, sizeof(Block));
	}
	return hash;
}

//high-level actions
constexpr unsigned threshold = 1 << 20;

int sendAmount(unsigned sender, unsigned receiver, unsigned amount) {

	Block transfer = generateBlock(generateTransfer(sender, receiver, amount), hashBlock(blockVector.back(), threshold));
	if (transfer.transaction.type == TransactionType::INVALID) {
		return -1;
	}

	Block receipt = generateBlock(generateReceipt(transfer), hashBlock(transfer, threshold));
	if (receipt.transaction.type == TransactionType::INVALID) {
		return -2;
	}

	Block ret = generateBlock(generateReturn(transfer, receipt), hashBlock(receipt, threshold));

	//once these are finallized, push to the blockchain
	blockVector.push_back(transfer);
	blockVector.push_back(receipt);

	//handle returns differently, since invalid returns can be generated by GENERATE blocks
	if (ret.transaction.type != TransactionType::INVALID) {
		blockVector.push_back(ret);
	}

	return 0;
}

int main(int argc, char* argv[]) {
	std::cout << "Blank size: " << blankSize << std::endl;
	std::cout << "Trans size: " << sizeof(Transaction) << std::endl;
	std::cout << "Block size: " << sizeof(Block) << std::endl;

	//genesis block
	blockVector.push_back(generateBlock(generateBlank("Kayne Ruse 2021!"), 42));
	sendAmount(0, 1, 50);
	sendAmount(0, 1, 50);
	sendAmount(0, 1, 50);
	sendAmount(0, 1, 50);
	sendAmount(1, 1, 50);
	sendAmount(1, 1, 50);
	sendAmount(1, 1, 50);
	sendAmount(1, 1, 50);
	sendAmount(1, 2, 75);
	sendAmount(1, 2, 75);
	sendAmount(1, 2, 75);
	sendAmount(1, 2, 75);

	//debug
	for (Block block : blockVector) {
		std::cout << block.index << " (" << block.prevHash << "): ";

		//print based on transaction type
		switch (block.transaction.type) {
			case TransactionType::INVALID:
				std::cout << "INVALID" << std::endl;
			break;

			case TransactionType::GENERATE:
				std::cout << "GENERATE " << block.transaction.transfer.receiverAccount << " received " << block.transaction.transfer.amount << std::endl;
			break;

			case TransactionType::TRANSFER:
				std::cout << "TRANSFER " << block.transaction.transfer.senderAccount << " sent " << block.transaction.transfer.amount << " to " << block.transaction.transfer.receiverAccount << std::endl;
			break;

			case TransactionType::RECEIPT:
				std::cout << "RECEIPT " << block.transaction.receipt.account << " now has " << block.transaction.receipt.balance << std::endl;
			break;

			default:
				std::cout << "error" << std::endl;
			break;
		}
	}

	return 0;
}