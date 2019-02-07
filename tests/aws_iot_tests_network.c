/*
 * Copyright (C) 2018 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * @file aws_iot_tests_network.c
 * @brief Common network function implementations for the tests.
 */

/* Build using a config header, if provided. */
#ifdef IOT_CONFIG_FILE
    #include IOT_CONFIG_FILE
#endif

/* Standard includes. */
#include <stdbool.h>
#include <string.h>

/* MQTT include. */
#include "aws_iot_mqtt.h"

/* POSIX+OpenSSL network include. */
#include "posix/iot_network_openssl.h"

/*-----------------------------------------------------------*/

/**
 * @brief Network interface setup function for the tests.
 *
 * Creates a global network connection to be used by the tests.
 * @return true if setup succeeded; false otherwise.
 *
 * @see #AwsIotTest_NetworkCleanup
 */
bool AwsIotTest_NetworkSetup( void );

/**
 * @brief Network interface cleanup function for the tests.
 *
 * @see #AwsIotTest_NetworkSetup
 */
void AwsIotTest_NetworkCleanup( void );

/**
 * @brief Network interface connect function for the tests.
 *
 * Creates a new network connection for use with MQTT.
 *
 * @param[out] pNewConnection The handle by which this new connection will be referenced.
 * @param[in] pMqttConnection The MQTT connection associated with the new network connection.
 *
 * @return true if a new network connection was successfully created; false otherwise.
 */
bool AwsIotTest_NetworkConnect( void * const pNewConnection,
                                AwsIotMqttConnection_t * pMqttConnection );

/**
 * @brief Network interface close connection function for the tests.
 *
 * @param[in] reason Currently unused.
 * @param[in] pDisconnectContext The connection to close. Pass NULL to close
 * the global network connection created by #AwsIotTest_NetworkSetup.
 *
 * @return Always returns #IOT_NETWORK_SUCCESS.
 */
IotNetworkError_t AwsIotTest_NetworkClose( int32_t reason,
                                           void * pDisconnectContext );

/**
 * @brief Network interface cleanup function for the tests.
 *
 * @param[in] pNetworkConnection The connection to destroy.
 */
void AwsIotTest_NetworkDestroy( void * pNetworkConnection );

/*-----------------------------------------------------------*/

/**
 * @brief Flag that tracks if the network connection is created.
 */
static bool _networkConnectionCreated = false;

/**
 * @brief The network connection shared among the tests.
 */
static IotNetworkConnectionOpenssl_t _networkConnection = IOT_NETWORK_CONNECTION_OPENSSL_INITIALIZER;

/**
 * @brief The MQTT network interface shared among the tests.
 */
AwsIotMqttNetIf_t _AwsIotTestNetworkInterface = AWS_IOT_MQTT_NETIF_INITIALIZER;

/**
 * @brief The MQTT connection shared among the tests.
 */
AwsIotMqttConnection_t _AwsIotTestMqttConnection = AWS_IOT_MQTT_CONNECTION_INITIALIZER;

/*-----------------------------------------------------------*/

bool AwsIotTest_NetworkSetup( void )
{
    /* Initialize the network library. */
    if( IotNetworkOpenssl_Init() != IOT_NETWORK_SUCCESS )
    {
        return false;
    }

    if( AwsIotTest_NetworkConnect( &_networkConnection,
                                   &_AwsIotTestMqttConnection ) == false )
    {
        IotNetworkOpenssl_Cleanup();

        return false;
    }

    /* Set the members of the network interface. */
    _AwsIotTestNetworkInterface.pDisconnectContext = NULL;
    _AwsIotTestNetworkInterface.disconnect = AwsIotTest_NetworkClose;
    _AwsIotTestNetworkInterface.pSendContext = ( void * ) &_networkConnection;
    _AwsIotTestNetworkInterface.send = IotNetworkOpenssl_Send;

    _networkConnectionCreated = true;

    return true;
}

/*-----------------------------------------------------------*/

void AwsIotTest_NetworkCleanup( void )
{
    /* Close the TCP connection to the server. */
    if( _networkConnectionCreated == true )
    {
        AwsIotTest_NetworkClose( 0, NULL );
        AwsIotTest_NetworkDestroy( &_networkConnection );
        _networkConnectionCreated = false;
    }

    /* Clean up the network library. */
    IotNetworkOpenssl_Cleanup();

    /* Clear the network interface. */
    ( void ) memset( &_AwsIotTestNetworkInterface, 0x00, sizeof( AwsIotMqttNetIf_t ) );
}

/*-----------------------------------------------------------*/

bool AwsIotTest_NetworkConnect( void * const pNewConnection,
                                AwsIotMqttConnection_t * pMqttConnection )
{
    IotNetworkServerInfoOpenssl_t serverInfo = IOT_NETWORK_SERVER_INFO_OPENSSL_INITIALIZER;
    IotNetworkCredentialsOpenssl_t * pCredentials = NULL;

    serverInfo.pHostName = AWS_IOT_TEST_SERVER;
    serverInfo.port = AWS_IOT_TEST_PORT;

    /* Set up TLS if the endpoint is secured. These tests should always use ALPN. */
    #if AWS_IOT_TEST_SECURED_CONNECTION == 1
        IotNetworkCredentialsOpenssl_t credentials = AWS_IOT_NETWORK_CREDENTIALS_OPENSSL_INITIALIZER;
        pCredentials = &credentials;

        /* Set credentials for secured connection. */
        credentials.pRootCaPath = AWS_IOT_TEST_ROOT_CA;
        credentials.pClientCertPath = AWS_IOT_TEST_CLIENT_CERT;
        credentials.pPrivateKeyPath = AWS_IOT_TEST_PRIVATE_KEY;
    #endif /* if AWS_IOT_TEST_SECURED_CONNECTION == 1 */

    /* Open a connection to the test server. */
    if( IotNetworkOpenssl_Create( &serverInfo,
                                  pCredentials,
                                  pNewConnection ) != IOT_NETWORK_SUCCESS )
    {
        return false;
    }

    /* Set the MQTT receive callback. */
    if( IotNetworkOpenssl_SetReceiveCallback( pNewConnection,
                                              AwsIotMqtt_ReceiveCallback,
                                              pMqttConnection ) != IOT_NETWORK_SUCCESS )
    {
        IotNetworkOpenssl_Close( 0, pNewConnection );
        IotNetworkOpenssl_Destroy( pNewConnection );

        return false;
    }

    return true;
}

/*-----------------------------------------------------------*/

IotNetworkError_t AwsIotTest_NetworkClose( int32_t reason,
                                           void * pDisconnectContext )
{
    IotNetworkConnectionOpenssl_t * pNetworkConnection = ( IotNetworkConnectionOpenssl_t * ) pDisconnectContext;

    ( void ) reason;

    /* Close the provided network handle; if that is NULL, close the
     * global network handle. */
    if( ( pNetworkConnection != NULL ) &&
        ( pNetworkConnection != &_networkConnection ) )
    {
        IotNetworkOpenssl_Close( 0, pNetworkConnection );
    }
    else if( _networkConnectionCreated == true )
    {
        IotNetworkOpenssl_Close( 0, &_networkConnection );
    }

    return IOT_NETWORK_SUCCESS;
}

/*-----------------------------------------------------------*/

void AwsIotTest_NetworkDestroy( void * pNetworkConnection )
{
    if( ( pNetworkConnection != NULL ) &&
        ( pNetworkConnection != &_networkConnection ) )
    {
        /* Wrap the network interface's destroy function. */
        IotNetworkOpenssl_Destroy( pNetworkConnection );
    }
    else
    {
        if( _networkConnectionCreated == true )
        {
            IotNetworkOpenssl_Destroy( &_networkConnection );
            _networkConnectionCreated = false;
        }
    }
}

/*-----------------------------------------------------------*/