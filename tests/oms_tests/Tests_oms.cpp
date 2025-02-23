#include <gtest/gtest.h>
#include "../../src/oms/OrderManagement.hpp"

class OrderManagementTests : public ::testing::Test 
{
    public:

        OrderManagement* cut;

    void SetUp() override 
    {
        cut = new OrderManagement();
    }

    void TearDown() override 
    {
        delete cut;
    }

    void GenerateOrder(int id)
    {
        Order order{
                id,
                "Filled",
                "AAPL",
                100.0,
                120.0};

        cut->addOrder(order);
    }

    void GeneratePosition(int id)
    {
        Position position{
                id,
                "AAPL",
                100.0,
                120.0};

        cut->addPosition(position);
    }

    Order order;
    Position position;
};

TEST_F(OrderManagementTests, CanBeInstantiated) 
{
    ASSERT_NE(cut, nullptr);
}

TEST_F(OrderManagementTests, AddOrder) 
{
    GenerateOrder(1);

    EXPECT_EQ(1, cut->getOrders().size());
}

TEST_F(OrderManagementTests, AddPosition) 
{
    GeneratePosition(1);

    EXPECT_EQ(1, cut->getPositions().size());
}

TEST_F(OrderManagementTests, AddMultipleOrders) 
{
    int numberOfOrders = 10;

    for(int i=0; i < numberOfOrders; ++i)
    {
        GenerateOrder(i);
    }
    
    EXPECT_EQ(numberOfOrders, cut->getOrders().size());
}

TEST_F(OrderManagementTests, AddMultiplePositions) 
{
    int numberOfPositions = 10;

    for(int i=0; i < numberOfPositions; ++i)
    {
        GeneratePosition(i);
    }
    
    EXPECT_EQ(numberOfPositions, cut->getPositions().size());
}


TEST_F(OrderManagementTests, RemoveOrders) 
{
    GenerateOrder(1);
    GenerateOrder(2);
    GenerateOrder(3);

    cut->removeOrder(1);
    
    EXPECT_EQ(2, cut->getOrders().size());
}

TEST_F(OrderManagementTests, RemovePositions) 
{
    GeneratePosition(1);
    GeneratePosition(2);
    GeneratePosition(3);

    cut->removePosition(1);
    
    EXPECT_EQ(2, cut->getPositions().size());
}

TEST_F(OrderManagementTests, RemoveCorrectOrderById) 
{
    GenerateOrder(1);
    GenerateOrder(2);
    GenerateOrder(3);

    cut->removeOrder(2);

    auto orders = cut->getOrders();
    
    EXPECT_EQ(orders[0].getId(), 1);
    EXPECT_EQ(orders[1].getId(), 3);
}

TEST_F(OrderManagementTests, RemoveCorrectPositionById) 
{
    GeneratePosition(1);
    GeneratePosition(2);
    GeneratePosition(3);

    cut->removePosition(2);

    auto positions = cut->getPositions();
    
    EXPECT_EQ(positions[0].getId(), 1);
    EXPECT_EQ(positions[1].getId(), 3);
}
